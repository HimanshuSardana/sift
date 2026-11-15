#include "scrape.h"

#include <libxml/HTMLparser.h>
#include <libxml/tree.h>

#include <cctype>
#include <memory>
#include <stdexcept>
#include <vector>

#include "fetch.h"

namespace {

struct DocFree {
    void operator()(xmlDoc* d) const { xmlFreeDoc(d); }
};

std::string lowerName(const xmlNode* n) {
    std::string name;
    if (!n->name) return name;
    for (const char* p = reinterpret_cast<const char*>(n->name); *p; ++p)
        name += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return name;
}

bool isChromeTag(const std::string& n) {
    static const char* const kChrome[] = {"script", "style",   "nav",    "header", "footer",
                                          "aside",  "form",    "iframe", "noscript", "svg",
                                          "button", "input",   "select", "textarea", "template",
                                          "figure", "audio",   "video",  "canvas", "object"};
    for (const char* c : kChrome)
        if (n == c) return true;
    return false;
}

bool isContainerTag(const std::string& n) {
    static const char* const kContainers[] = {"body", "article", "main", "div", "section", "td"};
    for (const char* c : kContainers)
        if (n == c) return true;
    return false;
}

int textSize(const xmlNode* n) {
    int total = 0;
    for (const xmlNode* c = n; c; c = c->next) {
        if (c->type == XML_TEXT_NODE || c->type == XML_CDATA_SECTION_NODE) {
            if (c->content) {
                for (const char* p = reinterpret_cast<const char*>(c->content); *p; ++p)
                    if (std::isspace(static_cast<unsigned char>(*p)) == 0) ++total;
            }
        } else if (c->type == XML_ELEMENT_NODE) {
            total += textSize(c->children);
        }
    }
    return total;
}

// Score a container by the prose it holds directly in paragraphs.
int paragraphScore(xmlNode* n) {
    int score = 0;
    for (xmlNode* c = n->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        const std::string name = lowerName(c);
        if (name == "p") {
            score += textSize(c->children);
        } else if (name == "article" || name == "div" || name == "section" || name == "main") {
            score += paragraphScore(c);
        }
    }
    return score;
}

bool isContainer(xmlNode* n) { return n->type == XML_ELEMENT_NODE && isContainerTag(lowerName(n)); }

// Descends into a single child container that holds nearly all of the prose,
// so a page wrapped in body/main still resolves to the actual article node.
xmlNode* innermost(xmlNode* node) {
    while (true) {
        const int score = paragraphScore(node);
        if (score == 0) return node;
        xmlNode* only = nullptr;
        for (xmlNode* c = node->children; c; c = c->next) {
            if (!isContainer(c)) continue;
            if (paragraphScore(c) * 10 < score * 9) continue;
            if (only) return node;  // two competing children: stop here
            only = c;
        }
        if (!only) return node;
        node = only;
    }
}

void collectContainers(xmlNode* n, std::vector<xmlNode*>& out) {
    for (xmlNode* c = n; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (isContainerTag(lowerName(c))) out.push_back(c);
        collectContainers(c->children, out);
    }
}

void prune(xmlNode* n) {
    xmlNode* c = n->children;
    while (c) {
        xmlNode* next = c->next;
        if (c->type == XML_ELEMENT_NODE && isChromeTag(lowerName(c)))
            xmlUnlinkNode(c), xmlFreeNode(c);
        else
            prune(c);
        c = next;
    }
}

std::string dump(xmlDoc* doc, xmlNode* node) {
    xmlBufferPtr buf = xmlBufferCreate();
    if (!buf) throw std::runtime_error("out of memory");
    xmlNodeDump(buf, doc, node, 0, 0);
    std::string out;
    if (xmlBufferContent(buf))
        out.assign(reinterpret_cast<const char*>(xmlBufferContent(buf)), xmlBufferLength(buf));
    xmlBufferFree(buf);
    return out;
}

}  // namespace

namespace scrape {

std::string extract(const std::string& html) {
    std::unique_ptr<xmlDoc, DocFree> doc(
        htmlReadMemory(html.c_str(), static_cast<int>(html.size()), nullptr, nullptr,
                       HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                           HTML_PARSE_NONET));
    if (!doc) throw std::runtime_error("page is not valid HTML");

    std::vector<xmlNode*> containers;
    collectContainers(xmlDocGetRootElement(doc.get()), containers);
    if (containers.empty()) throw std::runtime_error("page has no content container");

    xmlNode* best = nullptr;
    int best_score = 0;
    for (xmlNode* c : containers) {
        int score = paragraphScore(c);
        if (score > best_score) {
            best_score = score;
            best = c;
        }
    }
    if (!best) best = containers.front();
    best = innermost(best);

    prune(best);
    return dump(doc.get(), best);
}

std::string article(const std::string& url) { return extract(fetch::download(url)); }

}  // namespace scrape
