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
};

using Lines = std::vector<Line>;

struct Link {
    std::string text;  // anchor text, or the href when the anchor is empty
    std::string href;
};

// External links in document order, de-duplicated by href.
std::vector<Link> links(const std::string& html);

// <img src> values in document order, de-duplicated.
std::vector<std::string> imageUrls(const std::string& html);

// Renders an HTML fragment into styled lines wrapped to width columns:
// headings and <strong> bold, links underlined with their href appended,
// list items bulleted or numbered, quotes and code dimmed.
Lines renderHtml(const std::string& html, int width);

}  // namespace text
