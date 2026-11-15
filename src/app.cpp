#include "app.h"

#include <ncurses.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <stdexcept>

#include "fetch.h"
#include "scrape.h"
#include "text.h"

namespace {

// Standard eight terminal colours only, so the theme decides the exact shades.
constexpr short kPairTitle = 1;   // feed and post titles
constexpr short kPairMeta = 2;    // dates, authors, secondary hints
constexpr short kPairLink = 3;    // links and emphasised text
constexpr short kPairAccent = 4;  // headings and strong text
constexpr short kPairOk = 5;      // success status
constexpr short kPairErr = 6;     // failure status
constexpr short kPairBar = 7;     // focused pane header

// Modified arrow keys are missing from most terminfo entries; bind the common
// xterm and rxvt sequences to our own key codes.
constexpr int kCtlLeft = KEY_MAX + 1;
constexpr int kCtlRight = KEY_MAX + 2;

const char kHelpFeeds[] = " j/k move  a add  d delete  r refetch  / search  l open  q quit ";
const char kHelpPosts[] = " j/k move  l content  h feeds  / search  n next  f focus  q quit ";
const char kHelpContent[] = " j/k scroll  s scrape  / search  n next  f focus  h posts  q quit ";

std::string trim(const std::string& s) {
    auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    auto b = std::find_if_not(s.begin(), s.end(), ws);
    auto e = std::find_if_not(s.rbegin(), s.rend(), ws).base();
    return b < e ? std::string(b, e) : std::string();
}

std::string lowerCase(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string formatDate(const std::optional<long long>& ts) {
    if (!ts) return {};
    const std::time_t t = static_cast<std::time_t>(*ts);
    std::tm tm{};
    if (!localtime_r(&t, &tm)) return {};
    char buf[16] = {};
    if (strftime(buf, sizeof(buf), "%Y-%m-%d", &tm) == 0) return {};
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
void drawPanel(const Panel& p, const std::string& title, bool focused, bool color) {
    if (p.height < 2 || p.width < 4) return;

    const int right = p.left + p.width - 1;
    const int bottom = p.top + p.height - 1;

    int border = focused ? A_BOLD : A_DIM;
    if (color) border |= COLOR_PAIR(kPairTitle);

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

    if (title.empty() || p.width <= 8) return;

    std::string label = " " + title + " ";
    const int room = p.width - 4;
    if (static_cast<int>(label.size()) > room) label.resize(static_cast<size_t>(room));

    int title_attr = focused ? A_BOLD : A_DIM;
    if (color) title_attr |= COLOR_PAIR(focused ? kPairAccent : kPairMeta);
    attron(title_attr);
    mvaddnstr(p.top, p.left + 2, label.c_str(), room);
    attroff(title_attr);
}

}  // namespace

int App::pair(short slot) const { return color_ ? COLOR_PAIR(slot) : A_NORMAL; }

int App::attrFor(unsigned style) const {
    int attr = A_NORMAL;
    if (style & text::kBold) attr |= A_BOLD;
    if (style & text::kUnderline) attr |= A_UNDERLINE;
    if (style & text::kDim) attr |= A_DIM;

    short slot = 0;
    if (style & text::kUnderline) {
        slot = kPairLink;
    } else if (style & text::kBold) {
        slot = kPairAccent;
    } else if (style & text::kDim) {
        slot = kPairMeta;
    }
    if (slot) attr |= pair(slot);
    return attr;
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

    reloadFeeds();

    while (running_) {
        draw();
        int ch = getch();
        status_.clear();
        status_error_ = false;
        handleKey(ch);
    }

    endwin();
}

void App::draw() {
    erase();
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
    refresh();
}

void App::drawFeeds(int h, int w) {
    const Panel panel{0, 0, h - 1, w};
    drawPanel(panel, "sift - feeds (" + std::to_string(feeds_.size()) + ")", true, color_);

    const int rows = panel.innerRows();
    const int inner_x = panel.innerLeft();
    const int inner_w = panel.innerWidth();
    if (rows < 1 || inner_w < 4) {
        drawStatus(h, w);
        return;
    }

    if (feeds_.empty()) {
        attron(pair(kPairMeta));
        mvaddnstr(panel.innerTop(), inner_x + 1, "No feeds yet. Press 'a' to add one.", inner_w - 2);
        attroff(pair(kPairMeta));
        drawStatus(h, w);
        return;
    }

    if (selected_ < offset_) offset_ = selected_;
    if (selected_ >= offset_ + static_cast<size_t>(rows))
        offset_ = selected_ - static_cast<size_t>(rows) + 1;

    for (int r = 0; r < rows && offset_ + r < feeds_.size(); ++r) {
        const size_t i = offset_ + static_cast<size_t>(r);
        const Feed& f = feeds_[i];
        const std::string label = " " + (f.title.empty() ? f.url : f.title);
        const int y = panel.innerTop() + r;
        const bool sel = i == selected_;
        const int attr = sel ? A_REVERSE : pair(kPairTitle);
        attron(attr);
        if (sel) mvhline(y, inner_x, ' ', inner_w);
        mvaddnstr(y, inner_x, label.c_str(), inner_w);
        attroff(attr);
    }

    drawStatus(h, w);
}

void App::drawPosts(int h, int w) {
    const Feed* f = current();
    const std::string feed_title = f ? (f->title.empty() ? f->url : f->title) : std::string();

    const int left_w = leftWidth(w);
    const Panel left_panel{0, 0, h - 1, left_w};

    if (posts_.empty()) {
        drawPanel(left_panel, feed_title + " (0)", pane_ == Pane::Posts, color_);
        if (left_panel.innerRows() >= 2) {
            attron(pair(kPairMeta));
            mvaddnstr(left_panel.innerTop() + 1, left_panel.innerLeft() + 1,
                      "No posts yet. Press r to refetch.", left_panel.innerWidth() - 2);
            attroff(pair(kPairMeta));
        }
        drawStatus(h, w);
        return;
    }

    const Post* p = currentPost();
    const Panel right_panel{0, left_w, h - 1, w - left_w};
    drawPanel(left_panel, feed_title + " (" + std::to_string(posts_.size()) + ")",
              pane_ == Pane::Posts, color_);
    drawPanel(right_panel, p ? p->title : " post ", pane_ == Pane::Content, color_);

    const int rows = left_panel.innerRows();
    const int inner_x = left_panel.innerLeft();
    const int inner_w = left_panel.innerWidth();
    if (rows < 1 || inner_w < 4) {
        drawStatus(h, w);
        return;
    }

    if (post_selected_ < post_offset_) post_offset_ = post_selected_;
    if (post_selected_ >= post_offset_ + static_cast<size_t>(rows))
        post_offset_ = post_selected_ - static_cast<size_t>(rows) + 1;

    const int sel_attr = pane_ == Pane::Posts ? A_REVERSE : A_REVERSE | A_DIM;
    for (int r = 0; r < rows; ++r) {
        const size_t i = post_offset_ + static_cast<size_t>(r);
        if (i >= posts_.size()) break;
        const Post& post = posts_[i];
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
        mvaddnstr(y, title_x, post.title.c_str(), std::max(0, inner_x + inner_w - title_x));
    }

    drawPostBody(left_panel.top + 1, right_panel.innerLeft(), right_panel.innerWidth(),
                 right_panel.top + right_panel.height - 1);
    drawStatus(h, w);
}

void App::drawPostBody(int top, int x, int width, int bottom) {
    if (width <= 2) return;

    const Post* p = currentPost();
    if (!p) {
        mvaddnstr(top, x, "Select a post.", width);
        return;
    }

    int y = top;
    std::string meta = p->author;
    const std::string date = formatDate(p->published_at);
    if (!date.empty()) meta += (meta.empty() ? "" : "  ") + date;
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

    y++;
    const int avail = bottom - y;
    if (avail <= 0) return;

    const text::Lines lines =
        text::renderHtml(p->scraped.empty() ? p->content : p->scraped, width);
    if (lines.empty()) {
        attron(A_DIM);
        mvaddnstr(y, x, "(this post has no content)", width);
        attroff(A_DIM);
        return;
    }

    content_max_offset_ = lines.size() > static_cast<size_t>(avail)
                              ? lines.size() - static_cast<size_t>(avail)
                              : 0;
    content_offset_ = std::min(content_offset_, content_max_offset_);

    const int right = x + width;
    for (int r = 0; r < avail; ++r) {
        const size_t i = content_offset_ + static_cast<size_t>(r);
        if (i >= lines.size()) break;
        drawLine(y + r, x, right, lines[i]);
    }
}

void App::drawLine(int y, int x, int right, const text::Line& line) {
    int col = x;
    for (const text::Segment& seg : line.segments) {
        const int room = right - col;
        if (room <= 0) break;
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
    const int rows = status_.empty() ? h : h - 1;

    const text::Lines lines = contentLines();
    if (lines.empty()) {
        attron(A_DIM);
        mvaddnstr(0, x, "(this post has no content)", width);
        attroff(A_DIM);
        return;
    }

    content_max_offset_ = lines.size() > static_cast<size_t>(rows)
                              ? lines.size() - static_cast<size_t>(rows)
                              : 0;
    content_offset_ = std::min(content_offset_, content_max_offset_);

    for (int r = 0; r < rows; ++r) {
        const size_t i = content_offset_ + static_cast<size_t>(r);
        if (i >= lines.size()) break;
        drawLine(r, x, x + width, lines[i]);
    }

    if (!status_.empty()) {
        const int attr = color_ ? (status_error_ ? A_BOLD | pair(kPairErr) : pair(kPairOk)) : A_DIM;
        attron(attr);
        mvaddnstr(h - 1, x, status_.c_str(), width);
        attroff(attr);
    }
}

void App::handleFocusKey(int ch) {
    switch (ch) {
        case 'f':
        case 'h':
        case KEY_LEFT:
        case 27:
            focus_ = false;
            pane_ = Pane::Content;
            return;
        case 'j':
        case KEY_DOWN:
            if (content_offset_ < content_max_offset_) ++content_offset_;
            return;
        case 'k':
        case KEY_UP:
            if (content_offset_ > 0) --content_offset_;
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
            scrapeCurrentPost();
            return;
        case 'r':
            refetch();
            return;
        default:
            return;
    }
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
    if (!status_.empty() && color_) attr = status_error_ ? A_BOLD | pair(kPairErr) : pair(kPairOk);
    attron(attr);
    mvaddnstr(h - 1, 0, line.c_str(), w - 1);
    attroff(attr);
}

void App::handleKey(int ch) {
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
            if (n && selected_ + 1 < n) select(selected_ + 1);
            return;
        case 'k':
        case KEY_UP:
            if (n && selected_ > 0) select(selected_ - 1);
            return;
        case 'g':
        case KEY_HOME:
            if (n) select(0);
            return;
        case 'G':
        case KEY_END:
            if (n) select(n - 1);
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
            const Feed* f = current();
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
            if (n) openPosts();
            return;
        default:
            return;
    }
}

void App::handlePostsKey(int ch) {
    switch (ch) {
        case 27:  // Esc goes left, one level at a time
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
            if (pane_ == Pane::Posts) pane_ = Pane::Content;
            return;
        case 'j':
        case KEY_DOWN:
            if (pane_ == Pane::Content) {
                if (content_offset_ < content_max_offset_) ++content_offset_;
            } else if (post_selected_ + 1 < posts_.size()) {
                selectPost(post_selected_ + 1);
            }
            return;
        case 'k':
        case KEY_UP:
            if (pane_ == Pane::Content) {
                if (content_offset_ > 0) --content_offset_;
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
            focus_ = true;
            return;
        case 's':
            if (pane_ == Pane::Content) scrapeCurrentPost();
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
        if (n == 0) return;
        size_t i = selected_;
        for (size_t step = 0; step <= n; ++step, i = (i + n + dir) % n) {
            if (step == 0 && !inclusive) continue;
            const std::string label = feeds_[i].title.empty() ? feeds_[i].url : feeds_[i].title;
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
        if (n == 0) return;
        size_t i = post_selected_;
        for (size_t step = 0; step <= n; ++step, i = (i + n + dir) % n) {
            if (step == 0 && !inclusive) continue;
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
        if (idx < 0 || idx >= count) break;
        std::string text;
        for (const text::Segment& seg : lines[static_cast<size_t>(idx)].segments) text += seg.text;
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
    if (focus_) return std::max(10, std::min(w - 2, 80));
    return std::max(4, w - leftWidth(w) - 2);
}

text::Lines App::contentLines() const {
    const Post* p = currentPost();
    if (!p) return {};
    return text::renderHtml(p->scraped.empty() ? p->content : p->scraped, contentWidth());
}

int App::leftWidth(int w) const {
    int left = std::max(18, w * left_pct_ / 100);
    if (left > w - 12) left = std::max(10, w / 2);
    return std::min(left, w - 4);
}

void App::scrapeCurrentPost() {
    const Post* p = currentPost();
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
        if (html.empty()) throw std::runtime_error("no article content found");
        db::setPostScraped(id, html);
        for (Post& post : posts_)
            if (post.id == id) post.scraped = html;
        content_offset_ = 0;
        status_ = "Scraped " + std::to_string(html.size()) + " bytes.";
    } catch (const std::exception& e) {
        status_ = std::string("Scrape failed: ") + e.what();
        status_error_ = true;
    }
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
    const Feed* f = current();
    posts_ = f ? db::posts(f->id) : std::vector<Post>{};
    post_selected_ = posts_.empty() ? 0 : std::min(post_selected_, posts_.size() - 1);
    content_offset_ = 0;
    if (post_selected_ < post_offset_) post_offset_ = post_selected_;
}

void App::refetch() {
    const Feed* f = current();
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
        if (!fetched.title.empty()) db::setFeedTitle(id, fetched.title);
        db::upsertPosts(id, fetched.posts);
        db::markFetched(id, static_cast<long long>(std::time(nullptr)));
        status_ = "Fetched " + std::to_string(fetched.posts.size()) + " posts.";
    } catch (const std::exception& e) {
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
    if (feeds_.empty()) return;
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

const Feed* App::current() const {
    if (feeds_.empty() || selected_ >= feeds_.size()) return nullptr;
    return &feeds_[selected_];
}

const Post* App::currentPost() const {
    if (posts_.empty() || post_selected_ >= posts_.size()) return nullptr;
    return &posts_[post_selected_];
}

void App::selectPost(size_t index) {
    if (posts_.empty()) return;
    post_selected_ = std::min(index, posts_.size() - 1);
    content_offset_ = 0;
}

bool App::confirm(const std::string& question) {
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

bool App::prompt(const char* label, std::string& out) {
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
