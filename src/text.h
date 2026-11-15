#pragma once

#include <string>
#include <vector>

namespace text {

// Turns an HTML fragment into plain text: tags dropped, block elements
// become paragraph breaks, entities decoded, whitespace collapsed.
std::string htmlToText(const std::string& html);

// Greedy word wrap that keeps paragraph breaks and hard-splits long words.
std::vector<std::string> wrap(const std::string& text, int width);

}  // namespace text
