#pragma once

#include <string>
#include <vector>

namespace text {

enum Style : unsigned {
    kPlain = 0,
    kBold = 1u << 0,
    kDim = 1u << 1,
    kUnderline = 1u << 2,
};

struct Segment {
    std::string text;
    unsigned style = kPlain;
};

struct Line {
    std::vector<Segment> segments;
    std::string image;      // <img src> for a picture line, else empty
    std::string image_alt;  // alt text, used when the image cannot be drawn
};

using Lines = std::vector<Line>;

struct Link {
    std::string text;  // anchor text, or the href when the anchor is empty
    std::string href;
};

// External links in document order, de-duplicated by href.
std::vector<Link> links(const std::string& html);

struct Image {
    std::string src;
    std::string alt;
};

// <img> elements in document order, de-duplicated by src.
std::vector<Image> images(const std::string& html);

// Renders an HTML fragment into styled lines wrapped to width columns:
// headings and <strong> bold, links underlined with their href appended,
// list items bulleted or numbered, quotes and code dimmed.
Lines renderHtml(const std::string& html, int width);

}  // namespace text
