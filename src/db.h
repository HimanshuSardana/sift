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

struct Post {
    long long id = 0;
    long long feed_id = 0;
    std::string guid;
    std::string title;
    std::string url;
    std::string author;
    std::string content;
    std::optional<long long> published_at;
    bool read = false;
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

void setFeedTitle(long long feed_id, const std::string& title);
void markFetched(long long feed_id, long long unix_time);

// Inserts unseen posts and refreshes the content of known ones.
void upsertPosts(long long feed_id, const std::vector<Post>& posts);

std::vector<Post> posts(long long feed_id);

}  // namespace db
