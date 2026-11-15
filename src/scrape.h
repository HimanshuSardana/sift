#pragma once

#include <string>

namespace scrape {

// Picks the article container out of a page and drops chrome (nav, header,
// footer, aside, scripts, forms). Returns an HTML fragment.
std::string extract(const std::string& html);

// Downloads url and returns the extracted article HTML.
std::string article(const std::string& url);

}  // namespace scrape
