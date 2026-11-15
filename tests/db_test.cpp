#include "db.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace {

void require(bool cond, const char* what) {
    if (!cond) throw std::runtime_error(what);
}

}  // namespace

int main() {
    auto dir = std::filesystem::temp_directory_path() / "sift-db-test";
    std::filesystem::remove_all(dir);
    auto path = (dir / "test.db").string();

    db::open(path);
    require(db::feeds().empty(), "fresh db should have no feeds");

    db::addFeed("https://example.com/feed.xml", "Example");
    db::addFeed("https://other.test/rss", "Other");
    auto list = db::feeds();
    require(list.size() == 2, "expected 2 feeds");
    require(list[0].title == "Example" && list[1].title == "Other", "feeds sorted by title");

    // Same URL twice must not duplicate; the title gets updated.
    db::addFeed("https://example.com/feed.xml", "Example Renamed");
    list = db::feeds();
    require(list.size() == 2, "duplicate url should not insert");
    require(list[0].title == "Example Renamed", "duplicate url should update title");

    db::close();

    // Reopening an existing db keeps data.
    db::open(path);
    require(db::feeds().size() == 2, "feeds should persist across reopen");
    db::close();

    std::filesystem::remove_all(dir);
    std::puts("db tests passed");
    return 0;
}
