#include <stdexcept>
#include <string>

#include "scrape.h"

namespace {

void require(bool cond, const std::string& what) {
    if (!cond) throw std::runtime_error(what);
}

const char kPage[] = R"(<!doctype html>
<html><head><title>Blog</title><style>p{}</style></head>
<body>
  <header><a href="/">Home</a></header>
  <nav><a href="/a">Archive</a></nav>
  <div class="sidebar"><p>short teaser</p></div>
  <article>
    <h1>Real Article</h1>
    <p>The first paragraph of the article body, which is long enough to win.</p>
    <p>A second paragraph with more substance than the sidebar teaser.</p>
    <script>tracker();</script>
  </article>
  <footer>Copyright</footer>
</body></html>)";

}  // namespace

int main() {
    const std::string html = scrape::extract(kPage);

    require(html.find("Real Article") != std::string::npos, "keeps the article heading");
    require(html.find("first paragraph") != std::string::npos, "keeps article paragraphs");
    require(html.find("Archive") == std::string::npos, "drops nav");
    require(html.find("Home") == std::string::npos, "drops header");
    require(html.find("Copyright") == std::string::npos, "drops footer");
    require(html.find("tracker()") == std::string::npos, "drops script bodies");
    require(html.find("short teaser") == std::string::npos, "prefers the prose container");

    bool threw = false;
    try {
        scrape::extract("");
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "empty page should throw");

    std::puts("scrape tests passed");
    return 0;
}
