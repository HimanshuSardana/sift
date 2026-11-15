#pragma once

#include <optional>
#include <string>
#include <vector>

struct Feed {
    long long id = 0;
    std::string url;
    std::string title;
    std::optional<long long> last_fetched;
};

namespace db {

// Path under $XDG_DATA_HOME/sift (falls back to ~/.local/share/sift).
std::string defaultPath();

// Opens (creating if needed) the database at path and applies migrations.
void open(const std::string& path);
void close();

// Inserts the feed, or updates the title if the URL already exists.
long long addFeed(const std::string& url, const std::string& title);

std::vector<Feed> feeds();

}  // namespace db
