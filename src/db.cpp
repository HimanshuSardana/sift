#include "db.h"

#include <sqlite3.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <stdexcept>

namespace {

sqlite3* conn = nullptr;

void check(int rc, sqlite3* handle, const char* what) {
    if (rc != SQLITE_OK && rc != SQLITE_DONE && rc != SQLITE_ROW)
        throw std::runtime_error(std::string(what) + ": " + sqlite3_errmsg(handle));
}

struct Stmt {
    sqlite3_stmt* s = nullptr;

    Stmt(sqlite3* handle, const char* sql) {
        check(sqlite3_prepare_v2(handle, sql, -1, &s, nullptr), handle, "prepare");
    }
    ~Stmt() { sqlite3_finalize(s); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    void reset() {
        sqlite3_reset(s);
        sqlite3_clear_bindings(s);
    }
    void text(int i, const std::string& v) {
        check(sqlite3_bind_text(s, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT),
              sqlite3_db_handle(s), "bind text");
    }
    void integer(int i, long long v) {
        check(sqlite3_bind_int64(s, i, v), sqlite3_db_handle(s), "bind int");
    }
    void null(int i) { check(sqlite3_bind_null(s, i), sqlite3_db_handle(s), "bind null"); }
    void step() { check(sqlite3_step(s), sqlite3_db_handle(s), "step"); }
    bool row() {
        int rc = sqlite3_step(s);
        if (rc == SQLITE_ROW) return true;
        check(rc, sqlite3_db_handle(s), "step");
        return false;
    }
    std::string colText(int i) {
        const auto* p = sqlite3_column_text(s, i);
        return p ? reinterpret_cast<const char*>(p) : "";
    }
};

void exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(conn, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}

bool hasColumn(const char* table, const char* column);

void migrate() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS feeds (
            id              INTEGER PRIMARY KEY,
            url             TEXT NOT NULL UNIQUE,
            title           TEXT NOT NULL DEFAULT '',
            last_fetched_at INTEGER
        );
        CREATE TABLE IF NOT EXISTS posts (
            id           INTEGER PRIMARY KEY,
            feed_id      INTEGER NOT NULL REFERENCES feeds(id) ON DELETE CASCADE,
            guid         TEXT NOT NULL,
            title        TEXT NOT NULL DEFAULT '',
            url          TEXT NOT NULL DEFAULT '',
            author       TEXT NOT NULL DEFAULT '',
            published_at INTEGER,
            content      TEXT NOT NULL DEFAULT '',
            read_at      INTEGER,
            UNIQUE(feed_id, guid)
        );
        CREATE INDEX IF NOT EXISTS posts_by_feed ON posts(feed_id, published_at DESC);
    )");

    if (!hasColumn("posts", "scraped")) {
        exec("ALTER TABLE posts ADD COLUMN scraped TEXT NOT NULL DEFAULT ''");
        exec("ALTER TABLE posts ADD COLUMN scraped_at INTEGER");
    }
}

bool hasColumn(const char* table, const char* column) {
    Stmt q(conn, ("PRAGMA table_info(" + std::string(table) + ")").c_str());
    while (q.row())
        if (q.colText(1) == column) return true;
    return false;
}

std::optional<long long> colOptInt(sqlite3_stmt* s, int i) {
    if (sqlite3_column_type(s, i) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int64(s, i);
}

}  // namespace

namespace db {

std::string defaultPath() {
    std::filesystem::path base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::filesystem::path(home) / ".local" / "share";
    } else {
        throw std::runtime_error("cannot locate home directory");
    }
    base /= "sift";
    std::filesystem::create_directories(base);
    return (base / "sift.db").string();
}

void open(const std::string& path) {
    if (conn) return;
    if (auto parent = std::filesystem::path(path).parent_path(); !parent.empty())
        std::filesystem::create_directories(parent);
    check(sqlite3_open(path.c_str(), &conn), conn, "open");
    sqlite3_busy_timeout(conn, 5000);
    exec("PRAGMA foreign_keys = ON");  // feeds -> posts cascade
    migrate();
}

void close() {
    if (!conn) return;
    sqlite3_close(conn);
    conn = nullptr;
}

long long addFeed(const std::string& url, const std::string& title) {
    Stmt q(conn,
           "INSERT INTO feeds(url, title) VALUES(?, ?) "
           "ON CONFLICT(url) DO UPDATE SET title = excluded.title "
           "RETURNING id");
    q.text(1, url);
    q.text(2, title);
    q.step();
    return sqlite3_column_int64(q.s, 0);
}

std::vector<Feed> feeds() {
    Stmt q(conn, "SELECT id, url, title, last_fetched_at FROM feeds ORDER BY title, id");
    std::vector<Feed> out;
    while (q.row()) {
        Feed f;
        f.id = sqlite3_column_int64(q.s, 0);
        f.url = q.colText(1);
        f.title = q.colText(2);
        f.last_fetched = colOptInt(q.s, 3);
        out.push_back(std::move(f));
    }
    return out;
}

void removeFeed(long long feed_id) {
    Stmt q(conn, "DELETE FROM feeds WHERE id = ?");
    q.integer(1, feed_id);
    q.step();
}

void setFeedTitle(long long feed_id, const std::string& title) {
    Stmt q(conn, "UPDATE feeds SET title = ? WHERE id = ?");
    q.text(1, title);
    q.integer(2, feed_id);
    q.step();
}

void markFetched(long long feed_id, long long unix_time) {
    Stmt q(conn, "UPDATE feeds SET last_fetched_at = ? WHERE id = ?");
    q.integer(1, unix_time);
    q.integer(2, feed_id);
    q.step();
}

void upsertPosts(long long feed_id, const std::vector<Post>& posts) {
    if (posts.empty()) return;

    exec("BEGIN");
    try {
        Stmt q(conn,
               "INSERT INTO posts(feed_id, guid, title, url, author, published_at, content) "
               "VALUES(?, ?, ?, ?, ?, ?, ?) "
               "ON CONFLICT(feed_id, guid) DO UPDATE SET "
               "title = excluded.title, url = excluded.url, author = excluded.author, "
               "published_at = excluded.published_at, content = excluded.content");
        for (const Post& p : posts) {
            q.reset();
            q.integer(1, feed_id);
            q.text(2, p.guid);
            q.text(3, p.title);
            q.text(4, p.url);
            q.text(5, p.author);
            if (p.published_at) {
                q.integer(6, *p.published_at);
            } else {
                q.null(6);
            }
            q.text(7, p.content);
            q.step();
        }
    } catch (...) {
        exec("ROLLBACK");
        throw;
    }
    exec("COMMIT");
}

std::vector<Post> posts(long long feed_id) {
    Stmt q(conn,
           "SELECT id, guid, title, url, author, published_at, content, scraped, read_at "
           "FROM posts "
           "WHERE feed_id = ? ORDER BY published_at DESC, id DESC");
    q.integer(1, feed_id);
    std::vector<Post> out;
    while (q.row()) {
        Post p;
        p.id = sqlite3_column_int64(q.s, 0);
        p.feed_id = feed_id;
        p.guid = q.colText(1);
        p.title = q.colText(2);
        p.url = q.colText(3);
        p.author = q.colText(4);
        p.published_at = colOptInt(q.s, 5);
        p.content = q.colText(6);
        p.scraped = q.colText(7);
        p.read = sqlite3_column_type(q.s, 8) != SQLITE_NULL;
        out.push_back(std::move(p));
    }
    return out;
}

void setPostScraped(long long post_id, const std::string& html) {
    Stmt q(conn, "UPDATE posts SET scraped = ?, scraped_at = ? WHERE id = ?");
    q.text(1, html);
    q.integer(2, static_cast<long long>(std::time(nullptr)));
    q.integer(3, post_id);
    q.step();
}

}  // namespace db
