#include <stdexcept>
#include <string>
#include <vector>

#include "text.h"

namespace {

void require(bool cond, const std::string& what) {
    if (!cond) throw std::runtime_error(what);
}

void testTags() {
    const std::string in =
        "<h2>Title</h2><p>First <b>bold</b> sentence.</p>"
        "<p>Second<br>line</p><script>var x = 1;</script><style>p{}</style>";
    const std::string out = text::htmlToText(in);
    require(out == "Title\n\nFirst bold sentence.\n\nSecond\nline", "tag strip: " + out);
}

void testEntities() {
    require(text::htmlToText("a &amp; b &lt;c&gt; &quot;d&quot;") == "a & b <c> \"d\"",
            "named entities");
    require(text::htmlToText("x&nbsp;y &mdash; z") == "x y - z", "space and dash entities");
    require(text::htmlToText("caf&#233; &#x41;") == "café A", "numeric entities to utf-8");
    require(text::htmlToText("broken &notanentity; text") == "broken &notanentity; text",
            "unknown entity is left alone");
}

void testWhitespace() {
    require(text::htmlToText("  a   b \n\n\n  c  ") == "a b\n\nc", "collapse whitespace: " +
                text::htmlToText("  a   b \n\n\n  c  "));
    require(text::htmlToText("") == "", "empty input");
    require(text::htmlToText("<p></p>") == "", "empty paragraph");
    require(text::htmlToText("unterminated <b") == "unterminated", "unterminated tag");
}

void testWrap() {
    const auto lines = text::wrap("one two three four five", 9);
    const std::vector<std::string> expected = {"one two", "three", "four five"};
    require(lines == expected, "wrap to width 9");

    const auto paragraphs = text::wrap("a b\n\nc d", 10);
    const std::vector<std::string> expected_p = {"a b", "", "c d"};
    require(paragraphs == expected_p, "wrap keeps paragraph breaks");

    const auto long_word = text::wrap("abcdefghij", 4);
    const std::vector<std::string> expected_l = {"abcd", "efgh", "ij"};
    require(long_word == expected_l, "wrap hard-splits long words");

    require(text::wrap("anything", 0).empty(), "non-positive width yields nothing");

    bool too_long = false;
    for (const auto& l : text::wrap("the quick brown fox jumps over the lazy dog", 7))
        too_long = too_long || l.size() > 7;
    require(!too_long, "no wrapped line exceeds width");
}

}  // namespace

int main() {
    testTags();
    testEntities();
    testWhitespace();
    testWrap();
    std::puts("text tests passed");
    return 0;
}
