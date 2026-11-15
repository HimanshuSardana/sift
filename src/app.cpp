#include "app.h"

#include <ncurses.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>

#include "fetch.h"

namespace {

const char kHelpFeeds[] = " j/k move   a add feed   r refetch   Enter open   q quit ";
const char kHelpPosts[] = " h back   r refetch   q quit ";

std::string trim(const std::string& s) {
    auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    auto b = std::find_if_not(s.begin(), s.end(), ws);
    auto e = std::find_if_not(s.rbegin(), s.rend(), ws).base();
    return b < e ? std::string(b, e) : std::string();
}

}  // namespace

void App::run() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

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
    std::string header = " sift - " + (f ? (f->title.empty() ? f->url : f->title) : "") + " ";
    attron(A_REVERSE);
    mvhline(0, 0, ' ', w);
    mvaddnstr(0, 0, header.c_str(), w - 1);
    attroff(A_REVERSE);

    mvaddnstr(2, 2, (std::to_string(posts_.size()) + " posts stored.").c_str(), w - 3);
    drawStatus(h, w);
}

void App::drawStatus(int h, int w) {
    const std::string& line = status_.empty() ? (view_ == View::Feeds ? kHelpFeeds : kHelpPosts) : status_;
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
                view_ = View::Posts;
                offset_ = 0;
            }
            return;
        default:
            return;
    }
}

void App::handlePostsKey(int ch) {
    if (ch == 27 || ch == 'h' || ch == KEY_LEFT) {
        view_ = View::Feeds;
        offset_ = 0;
    } else if (ch == 'r') {
        refetch();
    }
}

void App::reloadPosts() {
    const Feed* f = current();
    posts_ = f ? db::posts(f->id) : std::vector<Post>{};
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
