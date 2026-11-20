#include "app.h"

#include <ncurses.h>

#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <ctime>
#include <stdexcept>

#include "fetch.h"
#include "image.h"
#include "scrape.h"
#include "text.h"

namespace {

// Standard eight terminal colours only, so the theme decides the exact shades.
constexpr short kPairTitle = 1;  // feed and post titles
constexpr short kPairMeta = 2;   // dates, authors, secondary hints
constexpr short kPairLink = 3;   // links and emphasised text
constexpr short kPairAccent = 4; // headings and strong text
constexpr short kPairOk = 5;     // success status
constexpr short kPairErr = 6;    // failure status
constexpr short kPairBar = 7;    // focused pane header

// Modified arrow keys are missing from most terminfo entries; bind the common
// xterm and rxvt sequences to our own key codes.
constexpr int ctrlKey(char c) { return c & 0x1f; }

constexpr int kCtlLeft = KEY_MAX + 1;
constexpr int kCtlRight = KEY_MAX + 2;

const char kHelpFeeds[] =
    " j/k move  a add  d delete  r refetch  / search  l open  q quit ";
const char kHelpPosts[] =
    " j/k move  l content  h feeds  f links  C-f focus  / search  q quit ";
const char kHelpContent[] =
    " j/k  s scrape  o open  i images  f links  C-f focus  q quit ";

std::string trim(const std::string &s) {
  auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
  auto b = std::find_if_not(s.begin(), s.end(), ws);
  auto e = std::find_if_not(s.rbegin(), s.rend(), ws).base();
  return b < e ? std::string(b, e) : std::string();
}

// Launches the post URL in the user's browser without blocking the UI.
// Tries the usual openers in turn; SIFT_OPENER overrides the preference.
bool launchOpener(const std::string &url) {
  static const bool reaping = [] {
    std::signal(SIGCHLD, SIG_IGN); // do not collect zombies
    return true;
  }();
  (void)reaping;

  const char *override = std::getenv("SIFT_OPENER");
  std::vector<std::string> commands;
  if (override && *override)
    commands.emplace_back(override);
  commands.emplace_back("xdg-open");
  commands.emplace_back("open");
  commands.emplace_back("sensible-browser");

  const pid_t pid = fork();
  if (pid < 0)
    return false;
  if (pid == 0) {
    setsid();
    const int devnull = ::open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
    }
    for (const std::string &cmd : commands)
      execlp(cmd.c_str(), cmd.c_str(), url.c_str(),
             static_cast<char *>(nullptr));
    _exit(127);
  }
  return true;
}

std::string lowerCase(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s)
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

std::string formatDate(const std::optional<long long> &ts) {
  if (!ts)
    return {};
  const std::time_t t = static_cast<std::time_t>(*ts);
  std::tm tm{};
  if (!localtime_r(&t, &tm))
    return {};
  char buf[16] = {};
  if (strftime(buf, sizeof(buf), "%Y-%m-%d", &tm) == 0)
    return {};
  return buf;
}

struct Panel {
  int top = 0;
  int left = 0;
  int height = 0;
  int width = 0;

  int innerTop() const { return top + 1; }
  int innerLeft() const { return left + 1; }
  int innerRows() const { return height - 2; }
  int innerWidth() const { return width - 2; }
};

// Borders the panel and cuts the title into the top border, like a tmux pane.
void drawPanel(const Panel &p, const std::string &title, bool focused,
               bool color) {
  if (p.height < 2 || p.width < 4)
    return;

  const int right = p.left + p.width - 1;
  const int bottom = p.top + p.height - 1;

  int border = focused ? A_BOLD : A_DIM;
  if (color)
    border |= COLOR_PAIR(kPairTitle);

  attron(border);
  mvhline(p.top, p.left + 1, ACS_HLINE, p.width - 2);
  mvhline(bottom, p.left + 1, ACS_HLINE, p.width - 2);
  mvvline(p.top + 1, p.left, ACS_VLINE, p.height - 2);
  mvvline(p.top + 1, right, ACS_VLINE, p.height - 2);
  mvaddch(p.top, p.left, ACS_ULCORNER);
  mvaddch(p.top, right, ACS_URCORNER);
  mvaddch(bottom, p.left, ACS_LLCORNER);
  mvaddch(bottom, right, ACS_LRCORNER);
  attroff(border);

  if (title.empty() || p.width <= 8)
    return;

  std::string label = " " + title + " ";
  const int room = p.width - 4;
  if (static_cast<int>(label.size()) > room)
    label.resize(static_cast<size_t>(room));

  int title_attr = focused ? A_BOLD : A_DIM;
  if (color)
    title_attr |= COLOR_PAIR(focused ? kPairAccent : kPairMeta);
  attron(title_attr);
  mvaddnstr(p.top, p.left + 2, label.c_str(), room);
  attroff(title_attr);
}

} // namespace

int App::pair(short slot) const { return color_ ? COLOR_PAIR(slot) : A_NORMAL; }

int App::attrFor(unsigned style) const {
  int attr = A_NORMAL;
  if (style & text::kBold)
    attr |= A_BOLD;
  if (style & text::kUnderline)
    attr |= A_UNDERLINE;
  if (style & text::kDim)
    attr |= A_DIM;

  short slot = 0;
  if (style & text::kUnderline) {
    slot = kPairLink;
  } else if (style & text::kBold) {
    slot = kPairAccent;
  } else if (style & text::kDim) {
    slot = kPairMeta;
  }
  if (slot)
    attr |= pair(slot);
  return attr;
}

void App::openUrl(const std::string &url) {
  single_ = true;
  single_url_ = url;
  focus_ = true;
  run();
}

bool App::refreshSingle() {
  status_ = "Fetching " + single_url_ + " ...";
  status_error_ = false;
  try {
    single_html_ = scrape::article(single_url_);
    content_offset_ = 0;
    status_.clear();
    return true;
  } catch (const std::exception &e) {
    status_ = std::string("Fetch failed: ") + e.what();
    status_error_ = true;
    return false;
  }
}

void App::run() {
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);
  set_escdelay(25);

  if (has_colors() && use_default_colors() == OK) {
    start_color();
    color_ = true;
    init_pair(kPairTitle, COLOR_CYAN, -1);
    init_pair(kPairMeta, COLOR_YELLOW, -1);
    init_pair(kPairLink, COLOR_BLUE, -1);
    init_pair(kPairAccent, COLOR_MAGENTA, -1);
    init_pair(kPairOk, COLOR_GREEN, -1);
    init_pair(kPairErr, COLOR_RED, -1);
    init_pair(kPairBar, COLOR_BLACK, COLOR_CYAN);
  }

  define_key("\033[1;5D", kCtlLeft);
  define_key("\033[1;5C", kCtlRight);
  define_key("\033[5D", kCtlLeft);
  define_key("\033[5C", kCtlRight);

  // The single-page reader never touches the sqlite store.
  if (single_) {
    draw();
    refreshSingle();
  } else {
    reloadFeeds();
  }

  while (running_) {
    draw();
    int ch = getch();
    status_.clear();
    status_error_ = false;
    handleKey(ch);
  }

  // kitty keeps a graphic until it is deleted, so a placed image would linger
  // over the shell after we exit.
  image::clear();
  endwin();
}

void App::draw() {
  erase();
  placements_.clear();
  int h = 0, w = 0;
  getmaxyx(stdscr, h, w);

  if (h < 2 || w < 20) {
    mvaddnstr(0, 0, "terminal too small", std::max(1, w - 1));
  } else if (focus_) {
    drawFocus(h, w);
  } else if (view_ == View::Feeds) {
    drawFeeds(h, w);
  } else {
    drawPosts(h, w);
  }

  if (palette_) {
    drawPalette(h, w);
  } else if (image_palette_) {
    drawImagePalette(h, w);
  } else {
    curs_set(0);
  }
  refresh();

  // Inline images go out after the text, otherwise ncurses paints over them.
  // Only touch the terminal when the placement actually changed.
  std::string sig;
  for (const Placement &p : placements_)
    sig += p.path + ":" + std::to_string(p.x) + "," + std::to_string(p.y) + "," +
           std::to_string(p.cols) + "," + std::to_string(p.rows) + ";";
  if (sig != placed_sig_) {
    image::clear();
    for (const Placement &p : placements_)
      image::place(p.path, p.x, p.y, p.cols, p.rows);
    placed_sig_ = sig;
  }
}

void App::drawFeeds(int h, int w) {
  const Panel panel{0, 0, h - 1, w};
  drawPanel(panel, "feeds (" + std::to_string(feeds_.size()) + ")", true,
            color_);

  const int rows = panel.innerRows();
  const int inner_x = panel.innerLeft();
  const int inner_w = panel.innerWidth();
  if (rows < 1 || inner_w < 4) {
    drawStatus(h, w);
    return;
  }

  if (feeds_.empty()) {
    attron(pair(kPairMeta));
    mvaddnstr(panel.innerTop(), inner_x + 1,
              "No feeds yet. Press 'a' to add one.", inner_w - 2);
    attroff(pair(kPairMeta));
    drawStatus(h, w);
    return;
  }

  if (selected_ < offset_)
    offset_ = selected_;
  if (selected_ >= offset_ + static_cast<size_t>(rows))
    offset_ = selected_ - static_cast<size_t>(rows) + 1;

  for (int r = 0; r < rows && offset_ + r < feeds_.size(); ++r) {
    const size_t i = offset_ + static_cast<size_t>(r);
    const Feed &f = feeds_[i];
    const std::string label = " " + (f.title.empty() ? f.url : f.title);
    const int y = panel.innerTop() + r;
    const bool sel = i == selected_;
    const int attr = sel ? A_REVERSE : pair(kPairTitle);
    attron(attr);
    if (sel)
      mvhline(y, inner_x, ' ', inner_w);
    mvaddnstr(y, inner_x, label.c_str(), inner_w);
    attroff(attr);
  }

  drawStatus(h, w);
}

void App::drawPosts(int h, int w) {
  const Feed *f = current();
  const std::string feed_title =
      f ? (f->title.empty() ? f->url : f->title) : std::string();

  const int left_w = leftWidth(w);
  const Panel left_panel{0, 0, h - 1, left_w};

  if (posts_.empty()) {
    drawPanel(left_panel, feed_title + " (0)", pane_ == Pane::Posts, color_);
    if (left_panel.innerRows() >= 2) {
      attron(pair(kPairMeta));
      mvaddnstr(left_panel.innerTop() + 1, left_panel.innerLeft() + 1,
                "No posts yet. Press r to refetch.",
                left_panel.innerWidth() - 2);
      attroff(pair(kPairMeta));
    }
    drawStatus(h, w);
    return;
  }

  const Post *p = currentPost();
  const Panel right_panel{0, left_w, h - 1, w - left_w};
  drawPanel(left_panel, feed_title + " (" + std::to_string(posts_.size()) + ")",
            pane_ == Pane::Posts, color_);
  drawPanel(right_panel, p ? p->title : " post ", pane_ == Pane::Content,
            color_);

  const int rows = left_panel.innerRows();
  const int inner_x = left_panel.innerLeft();
  const int inner_w = left_panel.innerWidth();
  if (rows < 1 || inner_w < 4) {
    drawStatus(h, w);
    return;
  }

  if (post_selected_ < post_offset_)
    post_offset_ = post_selected_;
  if (post_selected_ >= post_offset_ + static_cast<size_t>(rows))
    post_offset_ = post_selected_ - static_cast<size_t>(rows) + 1;

  const int sel_attr = pane_ == Pane::Posts ? A_REVERSE : A_REVERSE | A_DIM;
  for (int r = 0; r < rows; ++r) {
    const size_t i = post_offset_ + static_cast<size_t>(r);
    if (i >= posts_.size())
      break;
    const Post &post = posts_[i];
    const std::string date = formatDate(post.published_at);
    const int y = left_panel.innerTop() + r;
    const bool sel = i == post_selected_;

    if (sel) {
      attron(sel_attr);
      mvhline(y, inner_x, ' ', inner_w);
      mvaddnstr(y, inner_x, (" " + date + " " + post.title).c_str(), inner_w);
      attroff(sel_attr);
      continue;
    }

    mvaddnstr(y, inner_x, " ", 1);
    attron(pair(kPairMeta));
    mvaddnstr(y, inner_x + 1, date.c_str(), std::max(0, inner_w - 2));
    attroff(pair(kPairMeta));
    const int title_x = inner_x + 1 + static_cast<int>(date.size()) + 1;
    mvaddnstr(y, title_x, post.title.c_str(),
              std::max(0, inner_x + inner_w - title_x));
  }

  drawPostBody(left_panel.top + 1, right_panel.innerLeft(),
               right_panel.innerWidth(),
               right_panel.top + right_panel.height - 1);
  drawStatus(h, w);
}

// Reserves rows for the inline image and remembers where to place it. Returns
// the first row left for text.
// Paints the rendered article from content_offset_. Pictures show up as a
// placeholder line; 'i' opens the gallery that renders them.
void App::drawContent(int top, int x, int width, int bottom) {
  const text::Lines lines = contentLines();
  if (lines.empty()) {
    const char *msg = "(this post has no content)";
    if (single_)
      msg = single_html_.empty() ? "(fetching ...)" : "(this page has no readable content)";
    attron(A_DIM);
    mvaddnstr(top, x, msg, width);
    attroff(A_DIM);
    return;
  }

  const int avail = bottom - top;
  if (avail <= 0)
    return;

  content_max_offset_ = lines.size() > static_cast<size_t>(avail)
                            ? lines.size() - static_cast<size_t>(avail)
                            : 0;
  content_offset_ = std::min(content_offset_, content_max_offset_);

  int y = top;
  for (size_t i = content_offset_; i < lines.size() && y < bottom; ++i) {
    const text::Line &line = lines[i];

    if (!line.image.empty()) {
      std::string label = "[image";
      if (!line.image_alt.empty())
        label += ": " + line.image_alt;
      label += " - press i]";
      attron(A_DIM);
      mvaddnstr(y++, x, label.c_str(), width);
      attroff(A_DIM);
      continue;
    }

    drawLine(y++, x, x + width, line);
  }
}

void App::drawPostBody(int top, int x, int width, int bottom) {
  if (width <= 2)
    return;

  const Post *p = currentPost();
  if (!p) {
    mvaddnstr(top, x, "Select a post.", width);
    return;
  }

  int y = top;
  std::string meta = p->author;
  const std::string date = formatDate(p->published_at);
  if (!date.empty())
    meta += (meta.empty() ? "" : "  ") + date;
  if (!meta.empty()) {
    attron(pair(kPairMeta));
    mvaddnstr(y++, x, meta.c_str(), width);
    attroff(pair(kPairMeta));
  }
  if (!p->url.empty()) {
    const int url_attr = A_DIM | pair(kPairLink);
    attron(url_attr);
    mvaddnstr(y++, x, p->url.c_str(), width);
    attroff(url_attr);
  }

  drawContent(y + 1, x, width, bottom);
}

void App::drawLine(int y, int x, int right, const text::Line &line) {
  int col = x;
  for (const text::Segment &seg : line.segments) {
    const int room = right - col;
    if (room <= 0)
      break;
    const int attr = attrFor(seg.style);
    attron(attr);
    mvaddnstr(y, col, seg.text.c_str(), room);
    attroff(attr);
    col += static_cast<int>(seg.text.size());
  }
}

// Reading view: no panes, no borders, just the article wrapped to a
// comfortable measure and centred in the terminal.
void App::drawFocus(int h, int w) {
  const int width = std::max(10, std::min(w - 2, 80));
  const int x = std::max(1, (w - width) / 2);
  const int bottom = (single_ || !status_.empty()) ? h - 1 : h;

  int top = 0;
  if (single_) {
    attron(A_DIM);
    mvaddnstr(0, x, single_url_.c_str(), width);
    attroff(A_DIM);
    top = 2;
  }

  drawContent(top, x, width, bottom);

  std::string line = status_;
  int attr =
      color_ ? (status_error_ ? A_BOLD | pair(kPairErr) : pair(kPairOk)) : A_DIM;
  if (line.empty()) {
    if (!single_)
      return;
    line = " j/k scroll  / search  f links  i images  o open  r reload  q quit ";
    attr = A_DIM;
  }
  attron(attr);
  mvaddnstr(h - 1, x, line.c_str(), width);
  attroff(attr);
}

void App::handleFocusKey(int ch) {
  switch (ch) {
  case ctrlKey('F'):
  case 'h':
  case KEY_LEFT:
  case 27:
    if (single_) {
      running_ = false;
      return;
    }
    focus_ = false;
    pane_ = Pane::Content;
    return;
  case 'j':
  case KEY_DOWN:
    if (content_offset_ < content_max_offset_)
      ++content_offset_;
    return;
  case 'k':
  case KEY_UP:
    if (content_offset_ > 0)
      --content_offset_;
    return;
  case 'g':
  case KEY_HOME:
    content_offset_ = 0;
    return;
  case 'G':
  case KEY_END:
    content_offset_ = content_max_offset_;
    return;
  case '/':
    beginSearch();
    return;
  case 'n':
    findNext(1, false);
    return;
  case 'N':
    findNext(-1, false);
    return;
  case 's':
    if (single_)
      refreshSingle();
    else
      scrapeCurrentPost();
    return;
  case 'o':
    openInBrowser();
    return;
  case 'i':
    openImagePalette();
    return;
  case 'f':
    openLinkPalette();
    return;
  case 'r':
    if (single_)
      refreshSingle();
    else
      refetch();
    return;
  default:
    return;
  }
}

std::vector<size_t> App::imageMatches() const {
  const std::string needle = lowerCase(image_query_);
  std::vector<size_t> out;
  for (size_t i = 0; i < image_list_.size(); ++i) {
    const text::Image &img = image_list_[i];
    if (needle.empty() || contains(lowerCase(img.alt), needle) ||
        contains(lowerCase(img.src), needle))
      out.push_back(i);
  }
  return out;
}

// Fetches (or reads from the disk cache) the picture under the cursor so the
// right-hand pane can preview it.
void App::loadPreview() {
  preview_path_.clear();
  preview_w_ = preview_h_ = 0;
  preview_failed_ = false;

  const std::vector<size_t> matches = imageMatches();
  if (image_selected_ >= matches.size())
    return;

  const text::Image &img = image_list_[matches[image_selected_]];

  const std::string url = image::resolveUrl(img.src, currentUrl());
  if (url.empty() || !image::supported()) {
    preview_failed_ = true;
    return;
  }

  try {
    preview_path_ = image::cacheFile(url, preview_w_, preview_h_);
    if (preview_w_ <= 0 || preview_h_ <= 0)
      preview_failed_ = true;
  } catch (const std::exception &) {
    preview_failed_ = true;
  }
}

void App::openImagePalette() {
  image_list_ = text::images(currentHtml());
  if (image_list_.empty()) {
    status_ = single_ ? "This page has no images." : "This post has no images.";
    return;
  }

  image_query_.clear();
  image_selected_ = 0;
  image_palette_ = true;
  loadPreview();
}

void App::handleImagePaletteKey(int ch) {
  switch (ch) {
  case 27:
    image_palette_ = false;
    placements_.clear();
    return;
  case '\n':
  case '\r':
  case KEY_ENTER: {
    const std::vector<size_t> matches = imageMatches();
    if (matches.empty() || image_selected_ >= matches.size())
      return;
    const std::string url =
        image::resolveUrl(image_list_[matches[image_selected_]].src,
                          currentUrl());
    if (url.empty()) {
      return;
    }
    launchOpener(url);
    status_ = "Opened in browser: " + url;
    image_palette_ = false;
    return;
  }
  case ctrlKey('N'):
  case KEY_DOWN: {
    const std::vector<size_t> matches = imageMatches();
    if (image_selected_ + 1 < matches.size()) {
      ++image_selected_;
      loadPreview();
    }
    return;
  }
  case ctrlKey('P'):
  case KEY_UP:
    if (image_selected_ > 0) {
      --image_selected_;
      loadPreview();
    }
    return;
  case KEY_BACKSPACE:
  case 127:
  case 8:
    if (!image_query_.empty())
      image_query_.pop_back();
    image_selected_ = 0;
    loadPreview();
    return;
  case ctrlKey('U'):
    image_query_.clear();
    image_selected_ = 0;
    loadPreview();
    return;
  default:
    if (ch >= 32 && ch < 127) {
      image_query_ += static_cast<char>(ch);
      image_selected_ = 0;
      loadPreview();
    }
    return;
  }
}

// Two columns: the picture list on the left, a live preview on the right.
void App::drawImagePalette(int h, int w) {
  const std::vector<size_t> matches = imageMatches();
  if (matches.empty())
    image_selected_ = 0;
  else if (image_selected_ >= matches.size())
    image_selected_ = matches.size() - 1;

  int width = std::min(w - 4, 92);
  if (width < 30)
    width = std::max(12, w - 2);
  int height = std::min(h - 4, 22);
  if (height < 10)
    height = std::max(8, h - 2);

  const Panel box{std::max(0, (h - height) / 2), std::max(0, (w - width) / 2), height, width};
  drawPanel(box, "images (" + std::to_string(matches.size()) + "/" +
                     std::to_string(image_list_.size()) + ")",
            true, color_);

  const int rows = box.innerRows();
  const int inner_x = box.innerLeft();
  const int inner_w = box.innerWidth();
  if (rows < 6 || inner_w < 24)
    return;

  for (int r = 0; r < rows; ++r)
    mvhline(box.innerTop() + r, inner_x, ' ', inner_w);

  attron(A_BOLD);
  mvaddnstr(box.innerTop(), inner_x, ("> " + image_query_).c_str(), inner_w);
  attroff(A_BOLD);

  const int list_w = std::max(16, inner_w * 34 / 100);
  const int list_x = inner_x;
  const int list_top = box.innerTop() + 2;
  const int list_rows = rows - 3;
  const int preview_x = inner_x + list_w + 1;
  const int preview_w = inner_w - list_w - 1;

  attron(A_DIM);
  for (int y = box.innerTop() + 1; y < box.innerTop() + rows - 1; ++y)
    mvaddch(y, inner_x + list_w, ACS_VLINE);
  attroff(A_DIM);

  size_t offset = 0;
  if (image_selected_ >= static_cast<size_t>(list_rows))
    offset = image_selected_ - static_cast<size_t>(list_rows) + 1;

  for (int r = 0; r < list_rows; ++r) {
    const size_t i = offset + static_cast<size_t>(r);
    if (i >= matches.size())
      break;

    const text::Image &img = image_list_[matches[i]];
    const std::string name = img.alt.empty() ? img.src : img.alt;
    const int y = list_top + r;

    if (i == image_selected_) {
      attron(A_REVERSE);
      mvhline(y, list_x, ' ', list_w);
      mvaddnstr(y, list_x, (" " + name).c_str(), list_w);
      attroff(A_REVERSE);
    } else {
      mvaddnstr(y, list_x, " ", 1);
      attron(pair(kPairTitle));
      mvaddnstr(y, list_x + 1, name.c_str(), list_w - 1);
      attroff(pair(kPairTitle));
    }
  }

  // Preview: reserve the right column and place the picture after the refresh.
  const int preview_top = list_top;
  const int preview_rows_avail = rows - 3;
  if (preview_rows_avail > 2 && preview_w > 4 && !preview_path_.empty() && preview_w_ > 0) {
    const double aspect = static_cast<double>(preview_h_) / preview_w_;
    int img_rows = static_cast<int>(preview_w * aspect / 2.0 + 0.5);
    img_rows = std::max(3, std::min(img_rows, preview_rows_avail));

    placements_.push_back(Placement{preview_path_, preview_x, preview_top,
                                    std::min(preview_w, 80), img_rows});
  } else {
    const std::string note = preview_failed_ ? "(no preview)"
                                             : "(loading)";
    attron(A_DIM);
    mvaddnstr(preview_top, preview_x, note.c_str(), preview_w);
    attroff(A_DIM);
  }

  attron(A_DIM);
  mvaddnstr(box.innerTop() + rows - 1, inner_x,
            "^N/^P move  Enter open  Esc close", inner_w);
  attroff(A_DIM);
}

void App::openLinkPalette() {
  palette_links_ = text::links(currentHtml());
  if (palette_links_.empty()) {
    status_ = single_ ? "This page has no links." : "This post has no links.";
    return;
  }

  palette_query_.clear();
  palette_selected_ = 0;
  palette_ = true;
}

std::vector<size_t> App::paletteMatches() const {
  const std::string needle = lowerCase(palette_query_);
  std::vector<size_t> out;
  for (size_t i = 0; i < palette_links_.size(); ++i) {
    const text::Link &l = palette_links_[i];
    if (needle.empty() || contains(lowerCase(l.text), needle) ||
        contains(lowerCase(l.href), needle))
      out.push_back(i);
  }
  return out;
}

void App::openSelectedLink() {
  const std::vector<size_t> matches = paletteMatches();
  if (matches.empty()) {
    palette_ = false;
    status_ = "No matching link.";
    status_error_ = true;
    return;
  }
  if (palette_selected_ >= matches.size())
    palette_selected_ = matches.size() - 1;

  const std::string href = palette_links_[matches[palette_selected_]].href;
  palette_ = false;

  if (!launchOpener(href)) {
    status_ = "Could not launch a browser.";
    status_error_ = true;
    return;
  }
  status_ = "Opened in browser: " + href;
}

void App::handlePaletteKey(int ch) {
  switch (ch) {
  case 27:
    palette_ = false;
    return;
  case '\n':
  case '\r':
  case KEY_ENTER:
    openSelectedLink();
    return;
  case ctrlKey('N'):
  case KEY_DOWN: {
    const std::vector<size_t> matches = paletteMatches();
    if (palette_selected_ + 1 < matches.size())
      ++palette_selected_;
    return;
  }
  case ctrlKey('P'):
  case KEY_UP:
    if (palette_selected_ > 0)
      --palette_selected_;
    return;
  case KEY_BACKSPACE:
  case 127:
  case 8:
    if (!palette_query_.empty())
      palette_query_.pop_back();
    palette_selected_ = 0;
    return;
  case ctrlKey('U'):
    palette_query_.clear();
    palette_selected_ = 0;
    return;
  default:
    if (ch >= 32 && ch < 127) {
      palette_query_ += static_cast<char>(ch);
      palette_selected_ = 0;
    }
    return;
  }
}

void App::drawPalette(int h, int w) {
  const std::vector<size_t> matches = paletteMatches();
  if (matches.empty()) {
    palette_selected_ = 0;
  } else if (palette_selected_ >= matches.size()) {
    palette_selected_ = matches.size() - 1;
  }

  int width = std::min(w - 4, 76);
  if (width < 24)
    width = std::max(10, w - 2);
  int height = std::min(h - 4, 20);
  if (height < 8)
    height = std::max(6, h - 2);

  const Panel box{std::max(0, (h - height) / 2), std::max(0, (w - width) / 2),
                  height, width};
  drawPanel(box,
            "links (" + std::to_string(matches.size()) + "/" +
                std::to_string(palette_links_.size()) + ")",
            true, color_);

  const int rows = box.innerRows();
  const int inner_x = box.innerLeft();
  const int inner_w = box.innerWidth();
  if (rows < 4 || inner_w < 8)
    return;

  // The palette floats over the article, so blank its whole interior first.
  for (int r = 0; r < rows; ++r)
    mvhline(box.innerTop() + r, inner_x, ' ', inner_w);

  attron(A_BOLD);
  mvaddnstr(box.innerTop(), inner_x, ("> " + palette_query_).c_str(), inner_w);
  attroff(A_BOLD);

  const int visible = rows - 3;
  size_t offset = 0;
  if (palette_selected_ >= static_cast<size_t>(visible))
    offset = palette_selected_ - static_cast<size_t>(visible) + 1;

  for (int r = 0; r < visible; ++r) {
    const size_t i = offset + static_cast<size_t>(r);
    if (i >= matches.size())
      break;

    const text::Link &link = palette_links_[matches[i]];
    const std::string label = " " + link.text + "  " + link.href;
    const int y = box.innerTop() + 2 + r;

    if (i == palette_selected_) {
      attron(A_REVERSE);
      mvhline(y, inner_x, ' ', inner_w);
      mvaddnstr(y, inner_x, label.c_str(), inner_w);
      attroff(A_REVERSE);
    } else {
      mvaddnstr(y, inner_x, " ", 1);
      attron(pair(kPairTitle));
      mvaddnstr(y, inner_x + 1, link.text.c_str(), inner_w - 1);
      attroff(pair(kPairTitle));
      attron(A_DIM);
      mvaddnstr(y, inner_x + 2 + static_cast<int>(link.text.size()),
                link.href.c_str(),
                std::max(0, inner_w - 3 - static_cast<int>(link.text.size())));
      attroff(A_DIM);
    }
  }

  attron(A_DIM);
  mvaddnstr(box.innerTop() + rows - 1, inner_x,
            "^N/^P move  Enter open  Esc close", inner_w);
  attroff(A_DIM);

  curs_set(1);
  move(box.innerTop(),
       inner_x + std::min<int>(2 + static_cast<int>(palette_query_.size()),
                               std::max(0, inner_w - 1)));
}

void App::drawStatus(int h, int w) {
  std::string line = status_;
  if (line.empty()) {
    if (view_ == View::Feeds)
      line = kHelpFeeds;
    else
      line = pane_ == Pane::Content ? kHelpContent : kHelpPosts;
  }
  int attr = A_DIM;
  if (!status_.empty() && color_)
    attr = status_error_ ? A_BOLD | pair(kPairErr) : pair(kPairOk);
  attron(attr);
  mvaddnstr(h - 1, 0, line.c_str(), w - 1);
  attroff(attr);
}

void App::handleKey(int ch) {
  if (palette_) {
    handlePaletteKey(ch);
    return;
  }
  if (image_palette_) {
    handleImagePaletteKey(ch);
    return;
  }
  if (ch == 'q') {
    running_ = false;
    return;
  }
  if (focus_) {
    handleFocusKey(ch);
  } else if (view_ == View::Feeds) {
    handleFeedsKey(ch);
  } else {
    handlePostsKey(ch);
  }
}

void App::handleFeedsKey(int ch) {
  const size_t n = feeds_.size();

  switch (ch) {
  case 'j':
  case KEY_DOWN:
    if (n && selected_ + 1 < n)
      select(selected_ + 1);
    return;
  case 'k':
  case KEY_UP:
    if (n && selected_ > 0)
      select(selected_ - 1);
    return;
  case 'g':
  case KEY_HOME:
    if (n)
      select(0);
    return;
  case 'G':
  case KEY_END:
    if (n)
      select(n - 1);
    return;
  case '/':
    beginSearch();
    return;
  case 'n':
    findNext(1, false);
    return;
  case 'N':
    findNext(-1, false);
    return;
  case 'a': {
    std::string url;
    if (!prompt("Feed URL: ", url)) {
      status_ = "Add cancelled.";
      return;
    }
    const long long id = db::addFeed(url, url);
    reloadFeeds();
    selectFeedById(id);
    status_ = "Added feed: " + url;
    return;
  }
  case 'd': {
    const Feed *f = current();
    if (!f) {
      status_ = "No feed selected.";
      return;
    }
    const long long id = f->id;
    const std::string label = f->title.empty() ? f->url : f->title;
    if (!confirm("Delete feed \"" + label + "\" and its posts?")) {
      status_ = "Delete cancelled.";
      return;
    }
    db::removeFeed(id);
    reloadFeeds();
    reloadPosts();
    status_ = "Deleted feed: " + label;
    return;
  }
  case 'r':
    refetch();
    return;
  case 'l':
  case KEY_RIGHT:
  case '\n':
  case '\r':
  case KEY_ENTER:
    if (n)
      openPosts();
    return;
  default:
    return;
  }
}

void App::handlePostsKey(int ch) {
  switch (ch) {
  case 27: // Esc goes left, one level at a time
  case 'h':
  case KEY_LEFT:
    if (pane_ == Pane::Content) {
      pane_ = Pane::Posts;
    } else {
      view_ = View::Feeds;
      pane_ = Pane::Posts;
      offset_ = 0;
    }
    return;
  case 'l':
  case KEY_RIGHT:
    pane_ = Pane::Content;
    return;
  case '\n':
  case '\r':
  case KEY_ENTER:
    if (pane_ == Pane::Posts)
      pane_ = Pane::Content;
    return;
  case 'j':
  case KEY_DOWN:
    if (pane_ == Pane::Content) {
      if (content_offset_ < content_max_offset_)
        ++content_offset_;
    } else if (post_selected_ + 1 < posts_.size()) {
      selectPost(post_selected_ + 1);
    }
    return;
  case 'k':
  case KEY_UP:
    if (pane_ == Pane::Content) {
      if (content_offset_ > 0)
        --content_offset_;
    } else if (post_selected_ > 0) {
      selectPost(post_selected_ - 1);
    }
    return;
  case 'g':
  case KEY_HOME:
    if (pane_ == Pane::Content) {
      content_offset_ = 0;
    } else if (!posts_.empty()) {
      selectPost(0);
    }
    return;
  case 'G':
  case KEY_END:
    if (pane_ == Pane::Content) {
      content_offset_ = content_max_offset_;
    } else if (!posts_.empty()) {
      selectPost(posts_.size() - 1);
    }
    return;
  case '/':
    beginSearch();
    return;
  case 'n':
    findNext(1, false);
    return;
  case 'N':
    findNext(-1, false);
    return;
  case 'f':
    openLinkPalette();
    return;
  case ctrlKey('F'):
    focus_ = true;
    return;
  case 's':
    if (pane_ == Pane::Content)
      scrapeCurrentPost();
    return;
  case 'o':
    if (pane_ == Pane::Content)
      openInBrowser();
    return;
  case 'i':
    if (pane_ == Pane::Content)
      openImagePalette();
    return;
  case 'r':
    refetch();
    return;
  case kCtlLeft:
    left_pct_ = std::max(10, left_pct_ - 5);
    return;
  case kCtlRight:
    left_pct_ = std::min(60, left_pct_ + 5);
    return;
  default:
    return;
  }
}

void App::beginSearch() {
  std::string q;
  if (!prompt("Search: ", q)) {
    status_ = "Search cancelled.";
    return;
  }
  query_ = q;
  findNext(1, true);
}

void App::findNext(int dir, bool inclusive) {
  if (query_.empty()) {
    status_ = "Nothing to search for; press / first.";
    return;
  }
  const std::string needle = lowerCase(query_);
  const std::string miss = "No match: " + query_;

  if (view_ == View::Feeds) {
    const size_t n = feeds_.size();
    if (n == 0)
      return;
    size_t i = selected_;
    for (size_t step = 0; step <= n; ++step, i = (i + n + dir) % n) {
      if (step == 0 && !inclusive)
        continue;
      const std::string label =
          feeds_[i].title.empty() ? feeds_[i].url : feeds_[i].title;
      if (contains(lowerCase(label), needle)) {
        select(i);
        status_ = "Match: " + label;
        return;
      }
    }
    status_ = miss;
    status_error_ = true;
    return;
  }

  if (pane_ == Pane::Posts) {
    const size_t n = posts_.size();
    if (n == 0)
      return;
    size_t i = post_selected_;
    for (size_t step = 0; step <= n; ++step, i = (i + n + dir) % n) {
      if (step == 0 && !inclusive)
        continue;
      if (contains(lowerCase(posts_[i].title), needle)) {
        selectPost(i);
        status_ = "Match: " + posts_[i].title;
        return;
      }
    }
    status_ = miss;
    status_error_ = true;
    return;
  }

  const text::Lines lines = contentLines();
  if (lines.empty()) {
    status_ = "Nothing to search in this post.";
    return;
  }
  const long count = static_cast<long>(lines.size());
  const long start = static_cast<long>(content_offset_) + (inclusive ? 0 : dir);
  for (long k = 0; k < count; ++k) {
    const long idx = start + dir * k;
    if (idx < 0 || idx >= count)
      break;
    std::string text;
    for (const text::Segment &seg : lines[static_cast<size_t>(idx)].segments)
      text += seg.text;
    if (contains(lowerCase(text), needle)) {
      content_offset_ = static_cast<size_t>(std::max(0L, idx - 2));
      status_ = "Match at line " + std::to_string(idx + 1);
      return;
    }
  }
  status_ = miss;
  status_error_ = true;
}

int App::contentWidth() const {
  int w = getmaxx(stdscr);
  if (focus_)
    return std::max(10, std::min(w - 2, 80));
  return std::max(4, w - leftWidth(w) - 2);
}

text::Lines App::contentLines() const {
  return text::renderHtml(currentHtml(), contentWidth());
}

std::string App::currentHtml() const {
  if (single_)
    return single_html_;
  const Post *p = currentPost();
  if (!p)
    return {};
  return p->scraped.empty() ? p->content : p->scraped;
}

std::string App::currentUrl() const {
  if (single_)
    return single_url_;
  const Post *p = currentPost();
  return p ? p->url : std::string();
}

int App::leftWidth(int w) const {
  int left = std::max(18, w * left_pct_ / 100);
  if (left > w - 12)
    left = std::max(10, w / 2);
  return std::min(left, w - 4);
}

void App::scrapeCurrentPost() {
  const Post *p = currentPost();
  if (!p) {
    status_ = "No post selected.";
    return;
  }
  if (p->url.empty()) {
    status_ = "This post has no URL to scrape.";
    return;
  }

  const long long id = p->id;
  const std::string url = p->url;
  status_ = "Scraping " + url + " ...";
  draw();

  try {
    const std::string html = scrape::article(url);
    if (html.empty())
      throw std::runtime_error("no article content found");
    db::setPostScraped(id, html);
    for (Post &post : posts_)
      if (post.id == id)
        post.scraped = html;
    content_offset_ = 0;
      status_ = "Scraped " + std::to_string(html.size()) + " bytes.";
  } catch (const std::exception &e) {
    status_ = std::string("Scrape failed: ") + e.what();
    status_error_ = true;
  }
}

void App::openInBrowser() {
  const std::string url = currentUrl();
  if (url.empty()) {
    status_ = single_ ? "Nothing to open." : "No post selected.";
    status_error_ = true;
    return;
  }
  if (!launchOpener(url)) {
    status_ = "Could not launch a browser.";
    status_error_ = true;
    return;
  }
  status_ = "Opened in browser: " + url;
}

void App::openPosts() {
  reloadPosts();
  post_selected_ = 0;
  post_offset_ = 0;
  content_offset_ = 0;
  pane_ = Pane::Posts;
  view_ = View::Posts;
  offset_ = 0;
}

void App::reloadPosts() {
  const Feed *f = current();
  posts_ = f ? db::posts(f->id) : std::vector<Post>{};
  post_selected_ =
      posts_.empty() ? 0 : std::min(post_selected_, posts_.size() - 1);
  content_offset_ = 0;
  if (post_selected_ < post_offset_)
    post_offset_ = post_selected_;
}

void App::refetch() {
  const Feed *f = current();
  if (!f) {
    status_ = "No feed selected.";
    return;
  }

  const long long id = f->id;
  const std::string url = f->url;

  status_ = "Fetching " + url + " ...";
  draw();

  try {
    FetchedFeed fetched = fetch::feed(url);
    if (!fetched.title.empty())
      db::setFeedTitle(id, fetched.title);
    db::upsertPosts(id, fetched.posts);
    db::markFetched(id, static_cast<long long>(std::time(nullptr)));
    status_ = "Fetched " + std::to_string(fetched.posts.size()) + " posts.";
  } catch (const std::exception &e) {
    status_ = std::string("Fetch failed: ") + e.what();
    status_error_ = true;
  }

  reloadFeeds();
  selectFeedById(id);
  reloadPosts();
}

void App::reloadFeeds() {
  feeds_ = db::feeds();
  if (feeds_.empty()) {
    selected_ = 0;
    offset_ = 0;
    return;
  }
  selected_ = std::min(selected_, feeds_.size() - 1);
}

void App::select(size_t index) {
  if (feeds_.empty())
    return;
  selected_ = std::min(index, feeds_.size() - 1);
}

void App::selectFeedById(long long id) {
  for (size_t i = 0; i < feeds_.size(); ++i) {
    if (feeds_[i].id == id) {
      select(i);
      return;
    }
  }
}

const Feed *App::current() const {
  if (feeds_.empty() || selected_ >= feeds_.size())
    return nullptr;
  return &feeds_[selected_];
}

const Post *App::currentPost() const {
  if (posts_.empty() || post_selected_ >= posts_.size())
    return nullptr;
  return &posts_[post_selected_];
}

void App::selectPost(size_t index) {
  if (posts_.empty())
    return;
  post_selected_ = std::min(index, posts_.size() - 1);
  content_offset_ = 0;
}

bool App::confirm(const std::string &question) {
  int h = 0, w = 0;
  getmaxyx(stdscr, h, w);

  move(h - 1, 0);
  clrtoeol();
  attron(A_BOLD);
  addnstr((question + " (y/N) ").c_str(), w - 1);
  attroff(A_BOLD);
  refresh();

  const int ch = getch();
  return ch == 'y' || ch == 'Y';
}

bool App::prompt(const char *label, std::string &out) {
  int h = 0, w = 0;
  getmaxyx(stdscr, h, w);

  move(h - 1, 0);
  clrtoeol();
  attron(A_BOLD);
  addnstr(label, w - 1);
  attroff(A_BOLD);

  const int room = std::max(1, w - getcurx(stdscr) - 1);
  char buf[1024] = {};
  const int limit = std::min<int>(static_cast<int>(sizeof(buf)) - 1, room);

  curs_set(1);
  echo();
  const int rc = getnstr(buf, limit);
  noecho();
  curs_set(0);

  out = rc == OK ? trim(buf) : "";
  return !out.empty();
}
