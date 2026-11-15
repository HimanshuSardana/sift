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

    // Deleting a feed takes its posts with it.
    const long long other_id = list[1].id;
    Post p;
    p.guid = "g1";
    p.title = "A post";
    p.content = "<p>body</p>";
    db::upsertPosts(other_id, {p});
    require(db::posts(other_id).size() == 1, "post stored for feed");
    db::removeFeed(other_id);
    require(db::posts(other_id).empty(), "deleting a feed removes its posts");
    require(db::feeds().size() == 1, "only the deleted feed is gone");

    db::close();

    // Reopening an existing db keeps data.
    db::open(path);
    require(db::feeds().size() == 1, "feeds should persist across reopen");
    db::addFeed("https://other.test/rss", "Other");
    db::close();

    std::filesystem::remove_all(dir);
    std::puts("db tests passed");
    return 0;
}
