#include "db.h"

#include <sqlite3.h>

#include <cstdlib>
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

    void text(int i, const std::string& v) {
        check(sqlite3_bind_text(s, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT),
              sqlite3_db_handle(s), "bind");
    }
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

void migrate() {
    const char* schema = R"(
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
    )";
    char* err = nullptr;
    if (sqlite3_exec(conn, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("migrate: " + msg);
    }
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
        if (sqlite3_column_type(q.s, 3) != SQLITE_NULL)
            f.last_fetched = sqlite3_column_int64(q.s, 3);
        out.push_back(std::move(f));
    }
    return out;
}

}  // namespace db
