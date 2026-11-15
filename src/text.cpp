#include "text.h"

#include <libxml/HTMLparser.h>
#include <libxml/tree.h>

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int kPara = 0;
constexpr int kHeading = 1;
constexpr int kItem = 2;
constexpr int kQuote = 3;
constexpr int kPre = 4;

constexpr const char* kBreak = "\n";

struct Word {
    std::string text;
    unsigned style = text::kPlain;
};

struct Block {
    int kind = kPara;
    unsigned style = text::kPlain;
    std::string prefix;
    std::vector<Word> words;
    std::string raw;  // preformatted blocks keep their spacing verbatim
};

struct State {
    std::vector<Block> blocks;
    std::vector<std::pair<bool, int>> lists;  // (ordered, next number)
    bool open = false;
    bool preformatted = false;
};

struct DocFree {
    void operator()(htmlDocPtr d) const { xmlFreeDoc(d); }
};

std::string lowerName(const xmlNode* n) {
    std::string name;
    if (!n->name) return name;
    for (const char* p = reinterpret_cast<const char*>(n->name); *p; ++p)
        name += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return name;
}

std::string attr(const xmlNode* n, const char* name) {
    xmlChar* v = xmlGetProp(const_cast<xmlNode*>(n), BAD_CAST name);
    std::string out = v ? reinterpret_cast<const char*>(v) : "";
    if (v) xmlFree(v);
    return out;
}

bool isSkipTag(const std::string& n) {
    static const char* const kSkip[] = {"script", "style", "nav",    "header", "footer",
                                        "aside",  "form",  "iframe", "noscript", "svg",
                                        "button", "input", "select", "textarea", "template",
                                        "figure", "audio", "video", "canvas", "object"};
    for (const char* s : kSkip)
        if (n == s) return true;
    return false;
}

bool isHeading(const std::string& n) { return n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6'; }

bool isHttpUrl(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// libxml2 decodes entities already; this covers double-encoded feeds.
std::string decodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out += s[i];
            continue;
        }

        const std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp") {
            out += '&';
        } else if (ent == "lt") {
            out += '<';
        } else if (ent == "gt") {
            out += '>';
        } else if (ent == "quot") {
            out += '"';
        } else if (ent == "apos") {
            out += '\'';
        } else if (ent == "nbsp" || ent == "thinsp") {
            out += ' ';
        } else if (ent == "mdash" || ent == "ndash" || ent == "minus") {
            out += '-';
        } else if (ent == "hellip") {
            out += "...";
        } else if (ent == "rsquo" || ent == "lsquo") {
            out += '\'';
        } else if (ent == "ldquo" || ent == "rdquo") {
            out += '"';
        } else if (ent.size() > 1 && ent[0] == '#') {
            const bool hex = ent[1] == 'x' || ent[1] == 'X';
            char* end = nullptr;
            const unsigned long cp = std::strtoul(ent.c_str() + (hex ? 2 : 1), &end, hex ? 16 : 10);
            if (end && *end == '\0' && cp > 0) {
                appendUtf8(out, cp);
            } else {
                out += s[i];
                continue;
            }
        } else {
            out += s[i];
            continue;
        }
        i = semi;
    }
    return out;
}

Block& openBlock(State& st, int kind, std::string prefix, unsigned style = text::kPlain) {
    if (!st.open) {
        Block b;
        b.kind = kind;
        b.style = style;
        b.prefix = std::move(prefix);
        st.blocks.push_back(std::move(b));
        st.open = true;
    }
    return st.blocks.back();
}

void closeBlock(State& st) {
    st.open = false;
    st.preformatted = false;
}

void addText(State& st, const std::string& raw, unsigned style) {
    const std::string text = decodeEntities(raw);

    if (st.preformatted) {  // keep spaces, tabs and newlines exactly as authored
        openBlock(st, kPre, "", style).raw += text;
        return;
    }

    std::string word;

    auto flush = [&] {
        if (word.empty()) return;
        openBlock(st, kPara, "").words.push_back(Word{word, style});
        word.clear();
    };

    for (const char c : text) {
        if (st.preformatted && c == '\n') {
            flush();
            openBlock(st, kPara, "").words.push_back(Word{kBreak, style});
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            flush();
            continue;
        }
        word += c;
    }
    flush();
}

void walk(xmlNode* n, unsigned style, State& st) {
    for (; n; n = n->next) {
        if (n->type == XML_TEXT_NODE || n->type == XML_CDATA_SECTION_NODE) {
            if (n->content) addText(st, reinterpret_cast<const char*>(n->content), style);
            continue;
        }
        if (n->type != XML_ELEMENT_NODE) continue;

        const std::string name = lowerName(n);
        if (isSkipTag(name)) continue;

        if (name == "br") {
            if (st.open) openBlock(st, kPara, "").words.push_back(Word{kBreak, style});
            continue;
        }
        if (name == "hr") {
            closeBlock(st);
            continue;
        }
        if (isHeading(name)) {
            closeBlock(st);
            openBlock(st, kHeading, "");
            walk(n->children, style | text::kBold, st);
            closeBlock(st);
            continue;
        }
        if (name == "p") {
            closeBlock(st);
            openBlock(st, kPara, "");
            walk(n->children, style, st);
            closeBlock(st);
            continue;
        }
        if (name == "ul" || name == "ol") {
            closeBlock(st);
            st.lists.emplace_back(name == "ol", 1);
            walk(n->children, style, st);
            st.lists.pop_back();
            closeBlock(st);
            continue;
        }
        if (name == "li") {
            const bool ordered = !st.lists.empty() && st.lists.back().first;
            const int number = st.lists.empty() ? 1 : st.lists.back().second++;
            const std::string indent(2 * (st.lists.size() - 1), ' ');
            closeBlock(st);
            openBlock(st, kItem, indent + (ordered ? std::to_string(number) + ". " : "* "));
            walk(n->children, style, st);
            closeBlock(st);
            continue;
        }
        if (name == "blockquote") {
            closeBlock(st);
            openBlock(st, kQuote, "| ");
            walk(n->children, style | text::kDim, st);
            closeBlock(st);
            continue;
        }
        if (name == "pre") {
            closeBlock(st);
            st.preformatted = true;
            openBlock(st, kPre, "", style | text::kDim);
            walk(n->children, style | text::kDim, st);
            closeBlock(st);
            continue;
        }
        if (name == "b" || name == "strong") {
            walk(n->children, style | text::kBold, st);
            continue;
        }
        if (name == "i" || name == "em" || name == "cite") {
            walk(n->children, style | text::kUnderline, st);
            continue;
        }
        if (name == "code" || name == "tt" || name == "kbd") {
            walk(n->children, style | text::kDim, st);
            continue;
        }
        if (name == "a") {
            const std::string href = attr(n, "href");
            walk(n->children, style | text::kUnderline, st);
            if (isHttpUrl(href)) addText(st, " (" + href + ")", style | text::kDim);
            continue;
        }

        walk(n->children, style, st);  // transparent container
    }
}

std::string nodeText(xmlNode* n) {
    xmlChar* c = xmlNodeGetContent(n);
    std::string raw = c ? reinterpret_cast<const char*>(c) : "";
    if (c) xmlFree(c);

    std::string out;
    bool space = false;
    for (const char ch : raw) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += ch;
    }
    return out;
}

void walkLinks(xmlNode* n, std::vector<text::Link>& out) {
    for (; n; n = n->next) {
        if (n->type != XML_ELEMENT_NODE) continue;
        const std::string name = lowerName(n);
        if (name == "a") {
            const std::string href = attr(n, "href");
            if (isHttpUrl(href)) {
                std::string label = nodeText(n);
                if (label.empty()) label = href;
                bool seen = false;
                for (const text::Link& l : out) seen = seen || l.href == href;
                if (!seen) out.push_back(text::Link{label, href});
            }
        }
        walkLinks(n->children, out);
    }
}

void walkImages(xmlNode* n, std::vector<std::string>& out) {
    for (; n; n = n->next) {
        if (n->type != XML_ELEMENT_NODE) continue;
        const std::string name = lowerName(n);
        if (name == "img") {
            const std::string src = attr(n, "src");
            if (!src.empty()) {
                bool seen = false;
                for (const std::string& s : out) seen = seen || s == src;
                if (!seen) out.push_back(src);
            }
        }
        walkImages(n->children, out);
    }
}

void appendSegment(text::Line& line, const std::string& s, unsigned style) {
    if (s.empty()) return;
    if (!line.segments.empty() && line.segments.back().style == style) {
        line.segments.back().text += s;
        return;
    }
    line.segments.push_back(text::Segment{s, style});
}

std::string expandTabs(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '\t') {
            out += "    ";
        } else if (c != '\r') {
            out += c;
        }
    }
    return out;
}

void layoutPre(const Block& block, int width, text::Lines& out) {
    std::string raw = expandTabs(block.raw);
    while (!raw.empty() && raw.front() == '\n') raw.erase(raw.begin());
    while (!raw.empty() && (raw.back() == '\n' || raw.back() == ' ')) raw.pop_back();

    size_t pos = 0;
    while (true) {
        const size_t nl = raw.find('\n', pos);
        const std::string line =
            raw.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);

        for (size_t i = 0; i < line.size() || i == 0; i += static_cast<size_t>(width)) {
            text::Line shown;
            appendSegment(shown, line.substr(i, static_cast<size_t>(width)), block.style);
            out.push_back(std::move(shown));
            if (line.empty()) break;
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

void layout(const Block& block, int width, text::Lines& out) {
    if (block.kind == kPre) {
        layoutPre(block, width, out);
        return;
    }

    const std::string prefix = block.prefix;
    const std::string hang(prefix.size(), ' ');
    const int avail = width - static_cast<int>(prefix.size());
    if (avail <= 0) return;

    text::Line line;
    int used = 0;
    bool content = false;

    auto begin = [&](const std::string& pad) {
        line = text::Line{};
        appendSegment(line, pad, text::kDim);
        used = static_cast<int>(pad.size());
        content = false;
    };
    auto flush = [&] {
        out.push_back(line);
        begin(hang);
    };

    begin(prefix);

    for (const Word& w : block.words) {
        if (w.text == kBreak) {
            flush();
            continue;
        }

        std::string word = w.text;
        while (!word.empty()) {
            const int space = content ? 1 : 0;
            const int room = width - used - space;
            if (room <= 0) {
                flush();
                continue;
            }
            if (static_cast<int>(word.size()) <= room) {
                if (space) {
                    appendSegment(line, " ", w.style);
                    ++used;
                }
                appendSegment(line, word, w.style);
                used += static_cast<int>(word.size());
                content = true;
                break;
            }
            if (content) {
                flush();
                continue;
            }
            appendSegment(line, word.substr(0, static_cast<size_t>(room)), w.style);
            used += room;
            content = true;
            word.erase(0, static_cast<size_t>(room));
            flush();
        }
    }

    if (content || line.segments.size() > prefix.size()) out.push_back(line);
}

}  // namespace

namespace text {

std::vector<Link> links(const std::string& html) {
    std::vector<Link> out;
    std::unique_ptr<xmlDoc, DocFree> doc(
        htmlReadMemory(html.c_str(), static_cast<int>(html.size()), nullptr, nullptr,
                       HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                           HTML_PARSE_NONET));
    if (!doc) return out;
    xmlNode* root = xmlDocGetRootElement(doc.get());
    if (root) walkLinks(root, out);
    return out;
}

std::vector<std::string> imageUrls(const std::string& html) {
    std::vector<std::string> out;
    std::unique_ptr<xmlDoc, DocFree> doc(
        htmlReadMemory(html.c_str(), static_cast<int>(html.size()), nullptr, nullptr,
                       HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                           HTML_PARSE_NONET));
    if (!doc) return out;
    xmlNode* root = xmlDocGetRootElement(doc.get());
    if (root) walkImages(root, out);
    return out;
}

Lines renderHtml(const std::string& html, int width) {
    Lines out;
    if (width <= 0 || html.empty()) return out;

    std::unique_ptr<xmlDoc, DocFree> doc(
        htmlReadMemory(html.c_str(), static_cast<int>(html.size()), nullptr, nullptr,
                       HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING |
                           HTML_PARSE_NONET));
    if (!doc) {
        // Not markup at all: fall back to plain wrapped words.
        State st;
        addText(st, html, kPlain);
        for (const Block& b : st.blocks) layout(b, width, out);
        return out;
    }

    State st;
    xmlNode* root = xmlDocGetRootElement(doc.get());
    walk(root ? root->children : nullptr, kPlain, st);

    int previous_kind = -1;
    for (const Block& b : st.blocks) {
        if (b.words.empty() && b.raw.empty() && b.kind != kItem) continue;
        const bool tight = b.kind == kItem && previous_kind == kItem;
        if (!out.empty() && !tight) out.push_back(Line{});
        layout(b, width, out);
        previous_kind = b.kind;
    }

    while (!out.empty() && out.back().segments.empty()) out.pop_back();
    return out;
}

}  // namespace text
