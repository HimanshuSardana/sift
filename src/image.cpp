#include "image.h"

#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "fetch.h"
#include "text.h"

namespace {

constexpr int kImageId = 424242;      // our single inline image
constexpr size_t kChunk = 4096;       // kitty wants small escapes
constexpr double kCellAspect = 2.0;   // cells are about twice as tall as wide

std::string base64(const std::string& in) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);

    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t n = (static_cast<uint8_t>(in[i]) << 16) |
                           (static_cast<uint8_t>(in[i + 1]) << 8) |
                           static_cast<uint8_t>(in[i + 2]);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (i + 1 == in.size()) {
        const uint32_t n = static_cast<uint8_t>(in[i]) << 16;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const uint32_t n = (static_cast<uint8_t>(in[i]) << 16) |
                           (static_cast<uint8_t>(in[i + 1]) << 8);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

// tmux only forwards escapes inside a DCS wrapper, with inner ESCs doubled.
std::string throughTmux(const std::string& seq) {
    if (!std::getenv("TMUX")) return seq;
    std::string out = "\x1bPtmux;";
    for (const char c : seq) {
        if (c == '\x1b') {
            out += "\x1b\x1b";
        } else {
            out += c;
        }
    }
    out += "\x1b\\";
    return out;
}

void emit(const std::string& seq) { ::write(STDOUT_FILENO, seq.data(), seq.size()); }

bool readFile(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return !out.empty();
}

std::filesystem::path cacheDir() {
    std::filesystem::path base;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::filesystem::path(home) / ".cache";
    } else {
        throw std::runtime_error("cannot locate cache directory");
    }
    base /= "sift";
    base /= "images";
    std::filesystem::create_directories(base);
    return base;
}

std::string extensionFor(const std::string& url) {
    const size_t q = url.find_first_of("?#");
    const std::string path = q == std::string::npos ? url : url.substr(0, q);
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos) return ".img";
    std::string ext = path.substr(dot);
    if (ext.size() > 6) return ".img";
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

}  // namespace

namespace image {

bool supported() {
    if (const char* force = std::getenv("SIFT_IMAGES"); force && *force) {
        const std::string mode(force);
        if (mode == "off") return false;
        if (mode == "kitty") return true;
    }
    if (std::getenv("KITTY_WINDOW_ID")) return true;
    if (std::getenv("WEZTERM_EXECUTABLE") || std::getenv("GHOSTTY_RESOURCES_DIR")) return true;
    if (std::getenv("KONSOLE_VERSION")) return true;
    const char* term = std::getenv("TERM");
    return term && std::strstr(term, "kitty") != nullptr;
}

std::string resolveUrl(const std::string& src, const std::string& base_url) {
  if (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0)
    return src;
  if (src.rfind("//", 0) == 0)
    return "https:" + src;
  if (src.rfind("/", 0) == 0 && base_url.find("://") != std::string::npos) {
    const size_t scheme_end = base_url.find("://") + 3;
    const size_t host_end = base_url.find('/', scheme_end);
    return base_url.substr(0, host_end == std::string::npos ? base_url.size() : host_end) + src;
  }
  return {};
}

bool probeSize(const std::string& bytes, int& width, int& height) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const size_t n = bytes.size();

    if (n > 24 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G') {
        width = (p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19];
        height = (p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23];
        return width > 0 && height > 0;
    }
    if (n > 10 && std::memcmp(p, "GIF8", 4) == 0) {
        width = p[6] | (p[7] << 8);
        height = p[8] | (p[9] << 8);
        return width > 0 && height > 0;
    }
    if (n > 4 && p[0] == 0xFF && p[1] == 0xD8) {  // JPEG: walk to a SOFn marker
        size_t i = 2;
        while (i + 9 < n) {
            if (p[i] != 0xFF) {
                ++i;
                continue;
            }
            const unsigned char marker = p[i + 1];
            if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
                i += 2;
                continue;
            }
            const size_t len = (p[i + 2] << 8) | p[i + 3];
            const bool is_sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 &&
                                marker != 0xC8 && marker != 0xCC;
            if (is_sof) {
                height = (p[i + 5] << 8) | p[i + 6];
                width = (p[i + 7] << 8) | p[i + 8];
                return width > 0 && height > 0;
            }
            i += 2 + len;
        }
    }
    return false;
}

std::string cacheFile(const std::string& url, int& width, int& height) {
    width = height = 0;
    const std::string name =
        std::to_string(std::hash<std::string>{}(url)) + extensionFor(url);
    const std::filesystem::path path = cacheDir() / name;

    if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 0) {
        std::string head;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            char buf[4096];
            const size_t n = std::fread(buf, 1, sizeof(buf), f);
            head.assign(buf, n);
            std::fclose(f);
        }
        probeSize(head, width, height);
        return path.string();
    }

    const std::string bytes = fetch::download(url);
    std::filesystem::path tmp = path;
    tmp += ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) throw std::runtime_error("cannot write image cache");
    const size_t wrote = std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (wrote != bytes.size()) {
        std::filesystem::remove(tmp);
        throw std::runtime_error("short write to image cache");
    }
    std::filesystem::rename(tmp, path);
    probeSize(bytes, width, height);
    return path.string();
}

void place(const std::string& path, int x, int y, int cols, int rows) {
    std::string data;
    if (!readFile(path, data) || cols < 1 || rows < 1) return;

    const std::string b64 = base64(data);
    const std::string params = "a=T,f=100,i=" + std::to_string(kImageId) + ",c=" +
                               std::to_string(cols) + ",r=" + std::to_string(rows) +
                               ",C=1,z=0,q=2";

    // Cursor move and payload travel in one passthrough wrapper: tmux forwards
    // the enclosed sequence verbatim, so the outer terminal sees the move
    // immediately before the placement.
    std::string seq = "\x1b[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H";

    size_t offset = 0;
    bool first = true;
    while (offset < b64.size()) {
        const size_t n = std::min(kChunk, b64.size() - offset);
        const bool more = offset + n < b64.size();

        seq += "\x1b_G";
        if (first) {
            seq += params;
            seq += more ? ",m=1;" : ",m=0;";
        } else {
            // Continuation chunks carry only m=; a leading comma is invalid.
            seq += more ? "m=1;" : "m=0;";
        }
        seq += b64.substr(offset, n);
        seq += "\x1b\\";

        offset += n;
        first = false;
    }

    emit(throughTmux(seq));
}

void clear() {
    emit(throughTmux("\x1b_Ga=d,d=i,i=" + std::to_string(kImageId) + ",q=2\x1b\\"));
}

}  // namespace image
