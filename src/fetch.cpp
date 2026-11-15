#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "fetch.h"

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace {

constexpr size_t kMaxBody = 8u << 20;

std::string trim(const std::string& s) {
    auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    auto b = std::find_if_not(s.begin(), s.end(), ws);
    auto e = std::find_if_not(s.rbegin(), s.rend(), ws).base();
    return b < e ? std::string(b, e) : std::string();
}

bool isElem(const xmlNode* n, const char* name) {
    return n && n->type == XML_ELEMENT_NODE && xmlStrcasecmp(n->name, BAD_CAST name) == 0;
}

xmlNode* child(xmlNode* n, const char* name) {
    if (!n) return nullptr;
    for (xmlNode* c = n->children; c; c = c->next)
        if (isElem(c, name)) return c;
    return nullptr;
}

std::string textOf(const xmlNode* n) {
    if (!n) return {};
    xmlChar* t = xmlNodeGetContent(const_cast<xmlNode*>(n));
    std::string out = t ? reinterpret_cast<const char*>(t) : "";
    if (t) xmlFree(t);
    return trim(out);
}

std::string attr(const xmlNode* n, const char* name) {
    xmlChar* v = n ? xmlGetProp(const_cast<xmlNode*>(n), BAD_CAST name) : nullptr;
    std::string out = v ? reinterpret_cast<const char*>(v) : "";
    if (v) xmlFree(v);
    return trim(out);
}

xmlNode* alternateLink(xmlNode* entry) {
    for (xmlNode* c = entry->children; c; c = c->next) {
        if (!isElem(c, "link")) continue;
        const std::string rel = attr(c, "rel");
        if (rel.empty() || rel == "alternate") return c;
    }
    return nullptr;
}

std::optional<long long> parseTime(const std::string& raw) {
    std::string s = trim(raw);
    if (s.empty()) return std::nullopt;

    // strptime's %z does not accept zone names.
    for (const char* zone : {" GMT", " UTC", " UT"}) {
        const size_t n = std::strlen(zone);
        if (s.size() > n && s.compare(s.size() - n, n, zone) == 0) {
            s = s.substr(0, s.size() - n) + " +0000";
            break;
        }
    }

    static const char* kFormats[] = {
        "%a, %d %b %Y %H:%M:%S %z", "%a, %d %b %Y %H:%M:%S", "%a, %d %b %Y %H:%M %z",
        "%d %b %Y %H:%M:%S %z",     "%d %b %Y %H:%M:%S",     "%Y-%m-%dT%H:%M:%S%z",
        "%Y-%m-%dT%H:%M:%S",        "%Y-%m-%d %H:%M:%S",     "%Y-%m-%d",
    };

    for (const char* fmt : kFormats) {
        std::tm tm{};
        tm.tm_isdst = -1;
        const char* end = strptime(s.c_str(), fmt, &tm);
        if (!end) continue;
        std::string rest = trim(end);
        if (!rest.empty() && rest != "Z" && rest != "z") continue;
        const time_t t = timegm(&tm);
        if (t == static_cast<time_t>(-1)) continue;
        return static_cast<long long>(t - tm.tm_gmtoff);
    }
    return std::nullopt;
}

void finish(Post& p) {
    if (p.guid.empty()) p.guid = !p.url.empty() ? p.url : p.title + "|" + std::to_string(p.published_at.value_or(0));
    if (p.title.empty()) p.title = p.url;
}

struct CurlFree {
    void operator()(CURL* h) const { curl_easy_cleanup(h); }
};

struct DocFree {
    void operator()(xmlDoc* d) const { xmlFreeDoc(d); }
};

size_t writeBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    const size_t n = size * nmemb;
    if (out->size() + n > kMaxBody) return 0;
    out->append(ptr, n);
    return n;
}

}  // namespace

namespace fetch {

std::string download(const std::string& url) {
    static const bool curl_ready = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)curl_ready;

    std::unique_ptr<CURL, CurlFree> handle(curl_easy_init());
    if (!handle) throw std::runtime_error("curl init failed");

    CURL* c = handle.get();
    std::string body;
    char errbuf[CURL_ERROR_SIZE] = {};

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "sift/0.1");
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK)
        throw std::runtime_error(errbuf[0] ? errbuf : curl_easy_strerror(rc));

    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (status >= 400) throw std::runtime_error("HTTP " + std::to_string(status));
    if (body.empty()) throw std::runtime_error("empty response");
    return body;
}

FetchedFeed parse(const std::string& xml) {
    std::unique_ptr<xmlDoc, DocFree> doc(xmlReadMemory(xml.data(), static_cast<int>(xml.size()),
                                                       "feed.xml", nullptr,
                                                       XML_PARSE_RECOVER | XML_PARSE_NOERROR |
                                                           XML_PARSE_NOWARNING | XML_PARSE_NONET));
    if (!doc) throw std::runtime_error("feed is not valid XML");

    xmlNode* root = xmlDocGetRootElement(doc.get());
    if (!root) throw std::runtime_error("feed is empty");

    FetchedFeed out;

    if (isElem(root, "feed")) {  // Atom
        out.title = textOf(child(root, "title"));
        std::string feed_author;
        if (xmlNode* a = child(root, "author")) feed_author = textOf(child(a, "name"));

        for (xmlNode* e = root->children; e; e = e->next) {
            if (!isElem(e, "entry")) continue;
            Post p;
            p.guid = textOf(child(e, "id"));
            p.title = textOf(child(e, "title"));
            if (xmlNode* link = alternateLink(e)) p.url = attr(link, "href");
            if (xmlNode* a = child(e, "author")) p.author = textOf(child(a, "name"));
            if (p.author.empty()) p.author = feed_author;
            p.published_at = parseTime(textOf(child(e, "published")));
            if (!p.published_at) p.published_at = parseTime(textOf(child(e, "updated")));
            p.content = textOf(child(e, "content"));
            if (p.content.empty()) p.content = textOf(child(e, "summary"));
            finish(p);
            out.posts.push_back(std::move(p));
        }
        return out;
    }

    // RSS 2.0 keeps items under <channel>; RSS 1.0 (RDF) keeps them next to it.
    xmlNode* channel = isElem(root, "rss") ? child(root, "channel") : root;
    xmlNode* item_parent = isElem(root, "rss") ? channel : root;
    if (!channel) throw std::runtime_error("feed has no channel");

    out.title = textOf(child(channel, "title"));
    for (xmlNode* e = item_parent->children; e; e = e->next) {
        if (!isElem(e, "item")) continue;
        Post p;
        p.guid = textOf(child(e, "guid"));
        p.title = textOf(child(e, "title"));
        p.url = textOf(child(e, "link"));
        p.author = textOf(child(e, "author"));
        if (p.author.empty()) p.author = textOf(child(e, "creator"));
        p.published_at = parseTime(textOf(child(e, "pubDate")));
        if (!p.published_at) p.published_at = parseTime(textOf(child(e, "date")));

        const std::string description = textOf(child(e, "description"));
        const std::string encoded = textOf(child(e, "encoded"));
        p.content = encoded.size() > description.size() ? encoded : description;

        finish(p);
        out.posts.push_back(std::move(p));
    }
    return out;
}

FetchedFeed feed(const std::string& url) { return parse(download(url)); }

}  // namespace fetch
