#include <stdexcept>

#include "fetch.h"

namespace {

void require(bool cond, const char* what) {
    if (!cond) throw std::runtime_error(what);
}

const char kRss[] = R"(<?xml version="1.0"?>
<rss version="2.0" xmlns:content="http://purl.org/rss/1.0/modules/content/">
  <channel>
    <title> Example Blog </title>
    <item>
      <title>First post</title>
      <link>https://example.com/1</link>
      <guid>tag:example.com,2024:1</guid>
      <author>ada@example.com</author>
      <pubDate>Fri, 13 Feb 2009 23:31:30 +0000</pubDate>
      <description>short</description>
      <content:encoded><![CDATA[<p>much longer body</p>]]></content:encoded>
    </item>
    <item>
      <title>No guid post</title>
      <link>https://example.com/2</link>
      <pubDate>Sat, 14 Feb 2009 01:00:00 GMT</pubDate>
      <description>&lt;p&gt;escaped html&lt;/p&gt;</description>
    </item>
  </channel>
</rss>
)";

const char kAtom[] = R"(<?xml version="1.0" encoding="utf-8"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Atom Example</title>
  <author><name>Grace</name></author>
  <entry>
    <id>urn:uuid:1</id>
    <title>Atom post</title>
    <link rel="alternate" href="https://atom.test/1"/>
    <published>2024-03-01T10:00:00Z</published>
    <summary type="html">&lt;p&gt;atom body&lt;/p&gt;</summary>
  </entry>
</feed>
)";

void testRss() {
    FetchedFeed f = fetch::parse(kRss);
    require(f.title == "Example Blog", "rss channel title");
    require(f.posts.size() == 2, "rss item count");

    const Post& a = f.posts[0];
    require(a.guid == "tag:example.com,2024:1", "rss guid");
    require(a.title == "First post", "rss title");
    require(a.url == "https://example.com/1", "rss link");
    require(a.author == "ada@example.com", "rss author");
    require(a.content == "<p>much longer body</p>", "rss prefers content:encoded");
    require(a.published_at && *a.published_at == 1234567890LL, "rss pubDate to epoch");

    const Post& b = f.posts[1];
    require(b.guid == "https://example.com/2", "rss falls back to link as guid");
    require(b.content == "<p>escaped html</p>", "rss unescapes entities");
    require(b.published_at && *b.published_at == 1234573200LL, "rss GMT pubDate");
}

void testAtom() {
    FetchedFeed f = fetch::parse(kAtom);
    require(f.title == "Atom Example", "atom feed title");
    require(f.posts.size() == 1, "atom entry count");

    const Post& p = f.posts[0];
    require(p.guid == "urn:uuid:1", "atom id as guid");
    require(p.url == "https://atom.test/1", "atom alternate link");
    require(p.author == "Grace", "atom inherits feed author");
    require(p.content == "<p>atom body</p>", "atom summary content");
    require(p.published_at && *p.published_at == 1709287200LL, "atom ISO timestamp");
}

void testGarbage() {
    bool threw = false;
    try {
        fetch::parse("this is not xml at all <<<");
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "non-feed input should throw");
}

}  // namespace

int main() {
    testRss();
    testAtom();
    testGarbage();
    std::puts("parse tests passed");
    return 0;
}
