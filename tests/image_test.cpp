#include <cstdint>
#include <stdexcept>
#include <string>

#include "image.h"

namespace {

void require(bool cond, const std::string& what) {
  if (!cond) throw std::runtime_error(what);
}

std::string pngHeader(int w, int h) {
  std::string s;
  s += std::string("\x89PNG\r\n\x1a\n", 8);
  s += std::string("\x00\x00\x00\x0dIHDR", 8);
  for (int shift : {24, 16, 8, 0}) s += static_cast<char>((w >> shift) & 0xff);
  for (int shift : {24, 16, 8, 0}) s += static_cast<char>((h >> shift) & 0xff);
  s += std::string("\x08\x02\x00\x00\x00", 5);
  return s;
}

void testPng() {
  int w = 0, h = 0;
  require(image::probeSize(pngHeader(320, 180), w, h), "png header parsed");
  require(w == 320 && h == 180, "png dimensions");
}

void testGif() {
  std::string s("GIF89a");
  s += static_cast<char>(64);
  s += static_cast<char>(0);
  s += static_cast<char>(32);
  s += static_cast<char>(0);
  s += std::string("\x00\x00\x00", 3);
  int w = 0, h = 0;
  require(image::probeSize(s, w, h), "gif header parsed");
  require(w == 64 && h == 32, "gif dimensions");
}

void testJpeg() {
  std::string s;
  s += static_cast<char>(0xFF);
  s += static_cast<char>(0xD8);
  s += std::string("\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00", 18);
  s += std::string("\xff\xc0\x00\x11\x08", 5);
  s += static_cast<char>(0x00);  // height 200
  s += static_cast<char>(0xC8);
  s += static_cast<char>(0x01);  // width 320
  s += static_cast<char>(0x40);
  s += std::string("\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01", 10);
  int w = 0, h = 0;
  require(image::probeSize(s, w, h), "jpeg sof parsed");
  require(w == 320 && h == 200, "jpeg dimensions: " + std::to_string(w) + "x" + std::to_string(h));
}

void testGarbage() {
  int w = 0, h = 0;
  require(!image::probeSize("not an image at all", w, h), "garbage rejected");
  require(!image::probeSize("", w, h), "empty rejected");
  require(image::resolveUrl("https://y.test/i.png", "").empty() == false, "absolute kept");
  require(image::resolveUrl("//y.test/i.png", "") == "https://y.test/i.png",
          "protocol-relative resolved");
  require(image::resolveUrl("/i.png", "https://x.test/post") == "https://x.test/i.png",
          "root-relative resolved against the post host");
  require(image::resolveUrl("relative.png", "https://x.test/post").empty(),
          "bare relative paths are skipped");
}

}  // namespace

int main() {
  testPng();
  testGif();
  testJpeg();
  testGarbage();
  std::puts("image tests passed");
  return 0;
}
