#include "app.h"

#include <ncurses.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>

#include "fetch.h"
#include "text.h"

namespace {

const char kHelpFeeds[] = " j/k move   a add feed   r refetch   Enter open   q quit ";
const char kHelpPosts[] = " j/k move   l content   r refetch   Esc back   q quit ";
const char kHelpContent[] = " j/k scroll   h posts   r refetch   Esc back   q quit ";

std::string trim(const std::string& s) {
    auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    auto b = std::find_if_not(s.begin(), s.end(), ws);
    auto e = std::find_if_not(s.rbegin(), s.rend(), ws).base();
    return b < e ? std::string(b, e) : std::string();
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

int attrFor(unsigned style) {
    int attr = A_NORMAL;
    if (style & text::kBold) attr |= A_BOLD;
    if (style & text::kDim) attr |= A_DIM;
    if (style & text::kUnderline) attr |= A_UNDERLINE;
    return attr;
}

void drawBar(int y, int x, int width, const std::string& text, bool focused) {
    if (width <= 1) return;
    const int attr = focused ? A_REVERSE : A_REVERSE | A_DIM;
    attron(attr);
    mvhline(y, x, ' ', width);
    mvaddnstr(y, x, text.c_str(), width);
    attroff(attr);
}

}  // namespace

void App::run() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);

    reloadFeeds();

    while (running_) {
        draw();
        int ch = getch();
        status_.clear();
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
    } else if (view_ == View::Feeds) {
        drawFeeds(h, w);
    } else {
        drawPosts(h, w);
    }
    refresh();
}

void App::drawFeeds(int h, int w) {
    std::string header = " sift - feeds (" + std::to_string(feeds_.size()) + ") ";
    attron(A_REVERSE);
    mvhline(0, 0, ' ', w);
    mvaddnstr(0, 0, header.c_str(), w - 1);
    attroff(A_REVERSE);

    const int rows = h - 2;
    if (feeds_.empty()) {
        mvaddnstr(1, 2, "No feeds yet. Press 'a' to add one.", w - 3);
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
        const int y = 1 + r;
        const bool sel = i == selected_;
        if (sel) {
            attron(A_REVERSE);
            mvhline(y, 0, ' ', w);
        }
        mvaddnstr(y, 0, label.c_str(), w - 1);
        if (sel) attroff(A_REVERSE);
    }

    drawStatus(h, w);
}

void App::drawPosts(int h, int w) {
    const Feed* f = current();
    const std::string feed_title = f ? (f->title.empty() ? f->url : f->title) : std::string();

    int left = std::max(18, w * 20 / 100);
    if (left > w - 12) left = std::max(10, w / 2);
    const int right_x = left + 1;
    const int right_w = w - right_x;
    const int rows = h - 2;

    if (posts_.empty()) {
        drawBar(0, 0, w, " " + feed_title + " (0) ", pane_ == Pane::Posts);
        mvaddnstr(2, 2, "No posts yet. Press r to refetch.", w - 3);
        drawStatus(h, w);
        return;
    }

    drawBar(0, 0, left, " " + feed_title + " (" + std::to_string(posts_.size()) + ") ",
            pane_ == Pane::Posts);
    const Post* p = currentPost();
    drawBar(0, right_x, right_w, p ? " " + p->title + " " : " post ", pane_ == Pane::Content);

    for (int y = 0; y < 1 + rows; ++y) mvaddch(y, left, '|');

    if (post_selected_ < post_offset_) post_offset_ = post_selected_;
    if (post_selected_ >= post_offset_ + static_cast<size_t>(rows))
        post_offset_ = post_selected_ - static_cast<size_t>(rows) + 1;

    const int sel_attr = pane_ == Pane::Posts ? A_REVERSE : A_REVERSE | A_DIM;
    for (int r = 0; r < rows; ++r) {
        const size_t i = post_offset_ + static_cast<size_t>(r);
        if (i >= posts_.size()) break;
        const Post& post = posts_[i];
        const std::string label = " " + formatDate(post.published_at) + " " + post.title;
        const int y = 1 + r;
        const bool sel = i == post_selected_;
        if (sel) {
            attron(sel_attr);
            mvhline(y, 0, ' ', left);
        }
        mvaddnstr(y, 0, label.c_str(), left - 1);
        if (sel) attroff(sel_attr);
    }

    drawPostBody(1, right_x, right_w, h - 1);
    drawStatus(h, w);
}

void App::drawPostBody(int top, int x, int width, int bottom) {
    if (width <= 2) return;

    const Post* p = currentPost();
    if (!p) {
        mvaddnstr(top, x + 1, "Select a post.", width - 2);
        return;
    }

    int y = top;
    attron(A_BOLD);
    mvaddnstr(y++, x + 1, p->title.c_str(), width - 2);
    attroff(A_BOLD);

    std::string meta = p->author;
    const std::string date = formatDate(p->published_at);
    if (!date.empty()) meta += (meta.empty() ? "" : "  ") + date;
    if (!meta.empty()) {
        attron(A_DIM);
        mvaddnstr(y++, x + 1, meta.c_str(), width - 2);
        attroff(A_DIM);
    }
    if (!p->url.empty()) {
        attron(A_DIM);
        mvaddnstr(y++, x + 1, p->url.c_str(), width - 2);
        attroff(A_DIM);
    }

    y++;
    const int avail = bottom - y;
    if (avail <= 0) return;

    const text::Lines lines = text::renderHtml(p->content, width - 2);
    if (lines.empty()) {
        mvaddnstr(y, x + 1, "(this post has no content)", width - 2);
        return;
    }

    content_max_offset_ = lines.size() > static_cast<size_t>(avail)
                              ? lines.size() - static_cast<size_t>(avail)
                              : 0;
    content_offset_ = std::min(content_offset_, content_max_offset_);

    const int right = x + 1 + (width - 2);
    for (int r = 0; r < avail; ++r) {
        const size_t i = content_offset_ + static_cast<size_t>(r);
        if (i >= lines.size()) break;

        int col = x + 1;
        for (const text::Segment& seg : lines[i].segments) {
            const int room = right - col;
            if (room <= 0) break;
            const int attr = attrFor(seg.style);
            attron(attr);
            mvaddnstr(y + r, col, seg.text.c_str(), room);
            attroff(attr);
            col += static_cast<int>(seg.text.size());
        }
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
    attron(A_DIM);
    mvaddnstr(h - 1, 0, line.c_str(), w - 1);
    attroff(A_DIM);
}

void App::handleKey(int ch) {
    if (ch == 'q') {
        running_ = false;
        return;
    }
    if (view_ == View::Feeds) {
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
        case 'a': {
            std::string url;
            if (!prompt("Feed URL: ", url)) {
                status_ = "Add cancelled.";
                return;
            }
            long long id = db::addFeed(url, url);
            reloadFeeds();
            selectFeedById(id);
            status_ = "Added feed: " + url;
            return;
        }
        case 'r':
            refetch();
            return;
        case '\n':
        case '\r':
        case KEY_ENTER:
            if (n) {
                reloadPosts();
                post_selected_ = 0;
                post_offset_ = 0;
                content_offset_ = 0;
                pane_ = Pane::Posts;
                view_ = View::Posts;
                offset_ = 0;
            }
            return;
        default:
            return;
    }
}

void App::handlePostsKey(int ch) {
    switch (ch) {
        case 27:  // Esc returns to the feed list; h/l are reserved for focus.
            view_ = View::Feeds;
            offset_ = 0;
            pane_ = Pane::Posts;
            return;
        case 'h':
        case KEY_LEFT:
            pane_ = Pane::Posts;
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
        case 'r':
            refetch();
            return;
        default:
            return;
    }
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
