#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
namespace nova {
struct Glyph {
  std::string text;
  uint32_t code;
  int width;
};
inline std::vector<Glyph> glyphs(std::string_view text, bool lines = false) {
  std::vector<Glyph> out;
  for (size_t at = 0; at < text.size();) {
    auto c = static_cast<unsigned char>(text[at]);
    size_t n = c < 128            ? 1
               : (c & 224) == 192 ? 2
               : (c & 240) == 224 ? 3
               : (c & 248) == 240 ? 4
                                  : 0;
    uint32_t cp = n ? c & (n == 1 ? 127 : (1U << (7 - n)) - 1) : 0xFFFD;
    bool valid = n && at + n <= text.size();
    for (size_t i = 1; valid && i < n; ++i) {
      auto part = static_cast<unsigned char>(text[at + i]);
      if ((part & 192) != 128)
        valid = false;
      else
        cp = (cp << 6) | (part & 63);
    }
    valid = valid && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF) &&
            !(n == 2 && cp < 128) && !(n == 3 && cp < 2048) &&
            !(n == 4 && cp < 65536);
    std::string value = valid ? std::string(text.substr(at, n)) : "�";
    at += valid ? n : 1;
    if (!valid)
      cp = 0xFFFD;
    if (cp == '\n' && lines) {
      out.push_back({"\n", cp, -1});
      continue;
    }
    if (cp < 32 || (cp >= 127 && cp < 160) || (cp >= 0x202A && cp <= 0x202E) ||
        (cp >= 0x2066 && cp <= 0x2069)) {
      cp = 32;
      value = " ";
    }
    int width = 1;
    if ((cp >= 0x300 && cp <= 0x36F) || cp == 0x200D ||
        (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF))
      width = 0;
    else if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
             (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
             (cp >= 0xFF01 && cp <= 0xFF60) || cp == 0x2728 ||
             (cp >= 0x1F000 && cp <= 0x1FAFF) || cp >= 0x20000)
      width = 2;
    out.push_back({std::move(value), cp, width});
  }
  return out;
}
inline int columns(std::string_view text) {
  int n = 0;
  for (const auto &g : glyphs(text))
    n += g.width;
  return n;
}
inline std::string clean(std::string_view text, bool lines = false) {
  std::string out;
  for (const auto &g : glyphs(text, lines))
    out += g.text;
  return out;
}
inline std::string fit(std::string_view text, int width,
                       bool ellipsis = false) {
  width = std::max(0, width);
  bool cut = columns(text) > width;
  int limit = cut && ellipsis && width >= 2 ? width - 1 : width, n = 0;
  std::string out;
  for (const auto &g : glyphs(text)) {
    if (n + g.width > limit)
      break;
    out += g.text;
    n += g.width;
  }
  if (cut && ellipsis && width >= 2)
    out += "…";
  return out;
}
inline std::vector<std::string> wrap(std::string_view text, int width,
                                     bool words = false) {
  width = std::max(1, width);
  std::vector<std::string> out;
  std::string line;
  int n = 0;
  for (const auto &g : glyphs(text, true)) {
    if (g.width < 0) {
      out.push_back(line);
      line.clear();
      n = 0;
      continue;
    }
    if (n + g.width > width) {
      auto space = words ? line.find_last_of(' ') : std::string::npos;
      if (words && g.code == ' ') {
        out.push_back(line);
        line.clear();
        n = 0;
        continue;
      }
      if (space != std::string::npos && space > 0) {
        out.push_back(line.substr(0, space));
        line.erase(0, space + 1);
        n = columns(line);
      } else {
        out.push_back(line);
        line.clear();
        n = 0;
      }
    }
    if (g.width <= width) {
      line += g.text;
      n += g.width;
    }
  }
  out.push_back(line);
  return out;
}
inline std::string folded(std::string_view value) {
  std::string out;
  for (auto g : glyphs(value)) {
    if (g.code >= 'A' && g.code <= 'Z')
      out += char(g.code + 32);
    else if (g.code == 0x130 || g.code == 0x131)
      out += 'i';
    else if (g.code == 0x15E || g.code == 0x15F)
      out += 's';
    else if (g.code == 0x11E || g.code == 0x11F)
      out += 'g';
    else if (g.code == 0xDC || g.code == 0xFC)
      out += 'u';
    else if (g.code == 0xD6 || g.code == 0xF6)
      out += 'o';
    else if (g.code == 0xC7 || g.code == 0xE7)
      out += 'c';
    else
      out += g.text;
  }
  return out;
}
inline size_t previous(std::string_view value, size_t p) {
  if (!p)
    return 0;
  --p;
  while (p && (static_cast<unsigned char>(value[p]) & 192) == 128)
    --p;
  return p;
}
inline size_t next(std::string_view value, size_t p) {
  if (p == value.size())
    return p;
  ++p;
  while (p < value.size() &&
         (static_cast<unsigned char>(value[p]) & 192) == 128)
    ++p;
  return p;
}
struct Editor {
  std::string text;
  size_t cursor = 0;
  bool replace = false;
  void set(std::string value, bool selected = false) {
    text = std::move(value);
    cursor = text.size();
    replace = selected;
  }
  void insert(std::string value, size_t maximum = 8000) {
    value = clean(value, true);
    if (replace) {
      text.clear();
      cursor = 0;
      replace = false;
    }
    if (text.size() + value.size() > maximum)
      return;
    text.insert(cursor, value);
    cursor += value.size();
  }
  void erase() {
    if (replace) {
      set("");
      return;
    }
    auto p = previous(text, cursor);
    text.erase(p, cursor - p);
    cursor = p;
  }
  void del() {
    if (replace)
      set("");
    else
      text.erase(cursor, next(text, cursor) - cursor);
  }
  void left() {
    replace = false;
    cursor = previous(text, cursor);
  }
  void right() {
    replace = false;
    cursor = next(text, cursor);
  }
};
enum Tone { Text, Dim, Accent, Mint, Amber, Red, Violet, White };
enum Surface { Base, Sidebar, Rail, Raised, Selected, Composer };
struct Cell {
  std::string glyph = " ";
  Tone fg = Text;
  Surface bg = Base;
  bool continuation = false, bold = false;
};
struct Rect {
  int x, y, w, h;
  bool has(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};
class Canvas {
  std::vector<Cell> cells_;

public:
  int width, height;
  Canvas(int w, int h) : cells_(size_t(w * h)), width(w), height(h) {}
  Cell &cell(int x, int y) { return cells_[size_t(y * width + x)]; }
  void fill(Rect r, Surface bg) {
    for (int y = std::max(0, r.y); y < std::min(height, r.y + r.h); ++y)
      for (int x = std::max(0, r.x); x < std::min(width, r.x + r.w); ++x)
        cell(x, y) = {" ", Text, bg};
  }
  void put(int x, int y, std::string_view text, Tone fg = Text,
           Surface bg = Base, int max = -1, bool bold = false) {
    if (y < 0 || y >= height)
      return;
    int end = max < 0 ? width : std::min(width, x + max);
    for (const auto &g : glyphs(text)) {
      if (x + g.width > end)
        break;
      if (!g.width) {
        if (x > 0 && x <= width) {
          int at = x - 1;
          if (cell(at, y).continuation && at)
            --at;
          cell(at, y).glyph += g.text;
        }
        continue;
      }
      if (x >= 0 && x < width)
        cell(x, y) = {g.text, fg, bg, false, bold};
      if (g.width == 2 && x + 1 >= 0 && x + 1 < width)
        cell(x + 1, y) = {"", fg, bg, true, bold};
      x += g.width;
    }
  }
  void line(int x, int y, int n, Tone fg = Dim, Surface bg = Base) {
    for (int i = 0; i < n; ++i)
      put(x + i, y, "─", fg, bg);
  }
  void vertical(int x, int y, int n, Surface bg = Base) {
    for (int i = 0; i < n; ++i)
      put(x, y + i, "│", Dim, bg);
  }
  void box(Rect r, std::string title = {}, Tone tone = Accent,
           Surface bg = Raised) {
    fill(r, bg);
    line(r.x, r.y, r.w, tone, bg);
    line(r.x, r.y + r.h - 1, r.w, tone, bg);
    vertical(r.x, r.y + 1, r.h - 2, bg);
    vertical(r.x + r.w - 1, r.y + 1, r.h - 2, bg);
    put(r.x, r.y, "╭", tone, bg);
    put(r.x + r.w - 1, r.y, "╮", tone, bg);
    put(r.x, r.y + r.h - 1, "╰", tone, bg);
    put(r.x + r.w - 1, r.y + r.h - 1, "╯", tone, bg);
    if (!title.empty())
      put(r.x + 2, r.y, " " + fit(title, r.w - 6, true) + " ", tone, bg);
  }
  std::string render() const {
    static constexpr std::array<const char *, 8> fg = {
        "214;222;235", "127;146;169", "121;175;255", "103;224;187",
        "243;201;120", "255;124;146", "188;164;255", "245;248;255"};
    static constexpr std::array<const char *, 6> bg = {
        "16;22;34", "20;28;42", "12;17;27", "27;37;54", "37;54;75", "23;33;48"};
    std::ostringstream out;
    out << "\x1b[?25l\x1b[H";
    int old_fg = -1, old_bg = -1;
    bool old_bold = false;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto &c = cells_[size_t(y * width + x)];
        if (c.continuation)
          continue;
        if (old_fg != c.fg || old_bg != c.bg || old_bold != c.bold) {
          out << "\x1b[0;" << (c.bold ? "1;" : "") << "38;2;"
              << fg[size_t(c.fg)] << ";48;2;" << bg[size_t(c.bg)] << 'm';
          old_fg = c.fg;
          old_bg = c.bg;
          old_bold = c.bold;
        }
        out << c.glyph;
      }
      if (y + 1 < height)
        out << "\r\n";
    }
    out << "\x1b[0m";
    return out.str();
  }
};
} // namespace nova
