#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "db.h"

enum class View { Feeds, Posts };
enum class Pane { Posts, Content };

class App {
public:
    void run();

private:
    void draw();
    void drawFeeds(int h, int w);
    void drawPosts(int h, int w);
    void drawPostBody(int top, int x, int width, int bottom);
    void drawStatus(int h, int w);
    void handleKey(int ch);
    void handleFeedsKey(int ch);
    void handlePostsKey(int ch);

    void reloadFeeds();
    void reloadPosts();
    void refetch();
    void select(size_t index);
    void selectFeedById(long long id);
    void selectPost(size_t index);
    const Feed* current() const;
    const Post* currentPost() const;
    bool prompt(const char* label, std::string& out);

    std::vector<Feed> feeds_;
    std::vector<Post> posts_;
    View view_ = View::Feeds;
    Pane pane_ = Pane::Posts;
    size_t selected_ = 0;
    size_t offset_ = 0;
    size_t post_selected_ = 0;
    size_t post_offset_ = 0;
    size_t content_offset_ = 0;
    size_t content_max_offset_ = 0;
    bool running_ = true;
    std::string status_;
};
