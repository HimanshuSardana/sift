#pragma once

#include <string>
#include <vector>

#include "db.h"

struct FetchedFeed {
    std::string title;
    std::vector<Post> posts;
};

namespace fetch {

// HTTP GET of url with redirects; throws on transport or non-2xx status.
std::string download(const std::string& url);

// Parses RSS 2.0 / RSS 1.0 / Atom markup.
FetchedFeed parse(const std::string& xml);

FetchedFeed feed(const std::string& url);

}  // namespace fetch
