#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "db.h"

enum class View { Feeds, Posts };

class App {
public:
    void run();

private:
    void draw();
    void drawFeeds(int h, int w);
    void drawPosts(int h, int w);
    void drawStatus(int h, int w);
    void handleKey(int ch);
    void handleFeedsKey(int ch);
    void handlePostsKey(int ch);

    void reloadFeeds();
    void select(size_t index);
    const Feed* current() const;
    bool prompt(const char* label, std::string& out);

    std::vector<Feed> feeds_;
    View view_ = View::Feeds;
    size_t selected_ = 0;
    size_t offset_ = 0;
    bool running_ = true;
    std::string status_;
};
