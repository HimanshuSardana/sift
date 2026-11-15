#include <stdexcept>
#include <string>

#include "text.h"

namespace {

void require(bool cond, const std::string& what) {
    if (!cond) throw std::runtime_error(what);
}

std::string plain(const text::Lines& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += '\n';
        for (const auto& seg : lines[i].segments) out += seg.text;
    }
    return out;
}

unsigned styleAt(const text::Lines& lines, size_t line, const std::string& needle) {
    for (const auto& seg : lines[line].segments)
        if (seg.text.find(needle) != std::string::npos) return seg.style;
    return text::kPlain;
}

int widest(const text::Lines& lines) {
    int max = 0;
    for (const auto& l : lines) {
        int n = 0;
        for (const auto& s : l.segments) n += static_cast<int>(s.text.size());
        if (n > max) max = n;
    }
    return max;
}

void testStructure() {
    const auto lines = text::renderHtml(
        "<h2>Title</h2><p>First <b>bold</b> word.</p><p>Second paragraph.</p>", 40);
    require(plain(lines) == "Title\n\nFirst bold word.\n\nSecond paragraph.",
            "blocks separated by blank lines: " + plain(lines));
    require(styleAt(lines, 0, "Title") == text::kBold, "heading is bold");
    require(styleAt(lines, 2, "bold") == text::kBold, "inline strong is bold");
    require(styleAt(lines, 2, "First") == text::kPlain, "plain text stays plain");
}

void testEntitiesAndJunk() {
    const auto lines = text::renderHtml(
        "<p>a &amp; b &lt;c&gt; &quot;d&quot; caf&eacute; &#65;</p>", 60);
    require(plain(lines) == "a & b <c> \"d\" café A", "entities decoded: " + plain(lines));

    const auto junk = text::renderHtml(
        "<p>keep</p><script>var x = 1;</script><style>p{color:red}</style>"
        "<nav>menu</nav><footer>feet</footer>",
        40);
    require(plain(junk) == "keep", "script/style/nav/footer dropped: " + plain(junk));

    require(text::renderHtml("", 40).empty(), "empty input");
    require(text::renderHtml("plain words only", 40).size() == 1, "text without tags");
}

void testLinks() {
    const auto lines =
        text::renderHtml("<p>see <a href=\"https://example.com/x\">the docs</a></p>", 80);
    require(plain(lines) == "see the docs (https://example.com/x)", "link href appended");
    require(styleAt(lines, 0, "the docs") == text::kUnderline, "link underlined");

    const auto relative = text::renderHtml("<a href=\"/local\">local</a>", 40);
    require(plain(relative) == "local", "relative hrefs are not printed");
}

void testListsAndQuotes() {
    const auto ul = text::renderHtml("<ul><li>one</li><li>two</li></ul>", 40);
    require(plain(ul) == "* one\n* two", "bullets: " + plain(ul));

    const auto nested = text::renderHtml("<ul><li>a<ul><li>b</li></ul></li></ul>", 40);
    require(plain(nested) == "* a\n  * b", "nested bullets indent: " + plain(nested));

    const auto ol = text::renderHtml("<ol><li>first</li><li>second</li></ol>", 40);
    require(plain(ol) == "1. first\n2. second", "numbered items: " + plain(ol));

    const auto quote = text::renderHtml("<blockquote>quoted</blockquote>", 40);
    require(plain(quote) == "| quoted", "quote prefix: " + plain(quote));
    require(styleAt(quote, 0, "quoted") == text::kDim, "quote is dimmed");
}

void testWrapAndPre() {
    const auto lines = text::renderHtml("<p>one two three four five six</p>", 12);
    require(widest(lines) <= 12, "wrapped to width: " + plain(lines));
    require(plain(lines) == "one two\nthree four\nfive six", "greedy wrap: " + plain(lines));

    const auto long_word = text::renderHtml("<p>abcdefghijklmnop</p>", 6);
    require(widest(long_word) <= 6, "long word hard-split: " + plain(long_word));

    const auto pre = text::renderHtml("<pre>line one\nline two</pre>", 40);
    require(plain(pre) == "line one\nline two", "pre keeps breaks: " + plain(pre));

    const auto br = text::renderHtml("<p>a<br>b</p>", 40);
    require(plain(br) == "a\nb", "br forces a break: " + plain(br));
}

void testListWrapIndent() {
    const auto lines =
        text::renderHtml("<ul><li>alpha beta gamma delta epsilon</li></ul>", 16);
    require(widest(lines) <= 16, "wrapped list stays in width");
    require(plain(lines).rfind("* alpha", 0) == 0, "first line keeps bullet: " + plain(lines));
    require(plain(lines).find("\n  ") != std::string::npos, "continuation is indented");
}

}  // namespace

int main() {
    testStructure();
    testEntitiesAndJunk();
    testLinks();
    testListsAndQuotes();
    testWrapAndPre();
    testListWrapIndent();
    std::puts("text tests passed");
    return 0;
}
