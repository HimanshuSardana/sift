#include <clocale>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "app.h"
#include "db.h"

namespace {

const char* const kUsage =
    "usage: sift [url]\n"
    "\n"
    "  sift         open the feed dashboard\n"
    "  sift <url>   read a page in focus mode\n";

// Accepts bare hosts too, so "sift example.com/post" works.
std::string normalizeUrl(const std::string& arg) {
    if (arg.rfind("http://", 0) == 0 || arg.rfind("https://", 0) == 0)
        return arg;
    return "https://" + arg;
}

bool looksLikeUrl(const std::string& arg) {
    return arg.find(' ') == std::string::npos &&
           (arg.find('.') != std::string::npos || arg.find(':') != std::string::npos);
}

}  // namespace

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");

    std::string url;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            return 0;
        }
        if (!url.empty() || !looksLikeUrl(arg)) {
            std::fprintf(stderr, "sift: not a URL: %s\n%s", arg.c_str(), kUsage);
            return 2;
        }
        url = normalizeUrl(arg);
    }

    if (!url.empty()) {
        App app;
        app.openUrl(url);
        return 0;
    }

    try {
        db::open(db::defaultPath());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sift: %s\n", e.what());
        return 1;
    }

    App app;
    app.run();

    db::close();
    return 0;
}
