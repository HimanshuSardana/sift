#pragma once

#include <string>

namespace image {

// True when the terminal speaks the kitty graphics protocol (kitty, WezTerm,
// Ghostty, Konsole); SIFT_IMAGES=kitty|off overrides the guess.
bool supported();

// First usable http(s) image URL in the fragment, relative ones resolved
// against base_url. Empty when there is none.
std::string firstImageUrl(const std::string& html, const std::string& base_url);

// Pixel dimensions read from the PNG/JPEG/GIF header; false if unknown.
bool probeSize(const std::string& bytes, int& width, int& height);

// Downloads url into the cache (if not already there), returning the path and
// the pixel size of whatever landed there (0x0 when the header is unknown).
std::string cacheFile(const std::string& url, int& width, int& height);

// Places path at cell (x, y) scaled into cols x rows. The sequence is wrapped
// for tmux passthrough when running inside a multiplexer.
void place(const std::string& path, int x, int y, int cols, int rows);

// Removes every placement this program made.
void clear();

}  // namespace image
