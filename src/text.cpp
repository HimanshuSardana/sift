#include "text.h"

#include <cctype>
#include <cstddef>

namespace {

bool isBlockTag(const std::string& name) {
    static const char* const kBlocks[] = {
        "p",    "div",  "br",   "li",   "ul",   "ol",    "h1",   "h2",   "h3",
        "h4",   "h5",   "h6",   "tr",   "td",   "th",    "table", "blockquote",
        "pre",  "section", "article", "header", "footer", "figure", "figcaption", "hr",
    };
    for (const char* b : kBlocks)
        if (name == b) return true;
    return false;
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
            const std::string digits = ent.substr(hex ? 2 : 1);
            char* end = nullptr;
            const unsigned long cp = std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
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

std::string collapseWhitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());

    bool pending_space = false;
    int newlines = 0;

    for (const char c : s) {
        if (c == '\r') continue;
        if (c == '\n') {
            pending_space = false;
            if (out.empty()) continue;  // no leading blank lines from dropped tags
            ++newlines;
            if (newlines <= 2) out += '\n';
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!out.empty() && out.back() != '\n') pending_space = true;
            continue;
        }
        newlines = 0;
        if (pending_space) {
            out += ' ';
            pending_space = false;
        }
        out += c;
    }

    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

}  // namespace

namespace text {

std::string htmlToText(const std::string& html) {
    std::string text;
    text.reserve(html.size());

    size_t i = 0;
    while (i < html.size()) {
        if (html[i] != '<') {
            text += html[i++];
            continue;
        }

        if (html.compare(i, 4, "<!--") == 0) {
            const size_t end = html.find("-->", i + 4);
            i = end == std::string::npos ? html.size() : end + 3;
            continue;
        }

        const size_t close = html.find('>', i + 1);
        if (close == std::string::npos) break;

        std::string tag = html.substr(i + 1, close - i - 1);
        if (!tag.empty() && tag.front() == '/') tag.erase(tag.begin());

        std::string name;
        for (const char c : tag) {
            if (!std::isalnum(static_cast<unsigned char>(c))) break;
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        i = close + 1;

        if (name == "script" || name == "style") {
            const size_t end = html.find("</" + name, i);
            const size_t end_close = end == std::string::npos ? html.size() : html.find('>', end);
            i = (end_close == std::string::npos) ? html.size() : end_close + 1;
            continue;
        }
        if (isBlockTag(name)) text += name == "br" ? "\n" : "\n\n";
    }

    return collapseWhitespace(decodeEntities(text));
}

std::vector<std::string> wrap(const std::string& text, int width) {
    std::vector<std::string> lines;
    if (width <= 0) return lines;

    size_t pos = 0;
    while (true) {
        const size_t nl = text.find('\n', pos);
        const std::string para = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);

        if (para.empty()) {
            lines.emplace_back();
        } else {
            std::string line;
            size_t i = 0;
            while (i < para.size()) {
                while (i < para.size() && para[i] == ' ') ++i;
                const size_t j = para.find(' ', i);
                std::string word = para.substr(i, j == std::string::npos ? std::string::npos : j - i);
                i = (j == std::string::npos) ? para.size() : j;
                if (word.empty()) continue;

                while (word.size() > static_cast<size_t>(width)) {
                    if (!line.empty()) {
                        lines.push_back(line);
                        line.clear();
                    }
                    lines.push_back(word.substr(0, static_cast<size_t>(width)));
                    word.erase(0, static_cast<size_t>(width));
                }

                if (line.empty()) {
                    line = word;
                } else if (line.size() + 1 + word.size() <= static_cast<size_t>(width)) {
                    line += ' ';
                    line += word;
                } else {
                    lines.push_back(line);
                    line = word;
                }
            }
            if (!line.empty()) lines.push_back(line);
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }

    return lines;
}

}  // namespace text
