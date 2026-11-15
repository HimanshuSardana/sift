#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "db.h"
#include "text.h"

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
    void drawFocus(int h, int w);
    void drawPalette(int h, int w);
    void drawLine(int y, int x, int right, const text::Line& line);
    void drawStatus(int h, int w);
    void handleKey(int ch);
    void handleFeedsKey(int ch);
    void handlePostsKey(int ch);
    void handleFocusKey(int ch);
    void handlePaletteKey(int ch);

    void reloadFeeds();
    void beginSearch();
    void findNext(int dir, bool inclusive);
    int contentWidth() const;
    text::Lines contentLines() const;
    void toggleImages();
    int drawImageBlock(int x, int y, int width, int bottom);
    void openLinkPalette();
    void openSelectedLink();
    std::vector<size_t> paletteMatches() const;
    void openInBrowser();
    void openPosts();
    void reloadPosts();
    void refetch();
    void scrapeCurrentPost();
    void select(size_t index);
    void selectFeedById(long long id);
    void selectPost(size_t index);
    const Feed* current() const;
    const Post* currentPost() const;
    bool prompt(const char* label, std::string& out);
    int pair(short slot) const;
    int leftWidth(int w) const;
    int attrFor(unsigned style) const;
    bool confirm(const std::string& question);

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
    bool focus_ = false;
    bool images_ = false;
    std::string image_path_;
    int image_w_ = 0;
    int image_h_ = 0;
    std::string placed_sig_;
    bool pending_image_ = false;
    int pending_rect_[4] = {0, 0, 0, 0};
    bool palette_ = false;
    std::string palette_query_;
    size_t palette_selected_ = 0;
    std::vector<text::Link> palette_links_;
    std::string query_;
    int left_pct_ = 20;
    bool running_ = true;
    bool color_ = false;
    bool status_error_ = false;
    std::string status_;
};
