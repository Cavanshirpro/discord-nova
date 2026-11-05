#pragma once
#include "input.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace nova {
// Incremental xterm input: one read is not necessarily one key or UTF-8 glyph.
class TerminalInput {
  std::string bytes_;
  bool paste_ = false;
  static std::vector<int> numbers(std::string_view text) {
    std::vector<int> out;
    while (!text.empty()) {
      auto end = text.find(';');
      auto word = text.substr(0, end);
      int value = 0;
      auto result = std::from_chars(word.data(), word.data() + word.size(), value);
      if (result.ec != std::errc{} || result.ptr != word.data() + word.size() || value > 100000)
        return {};
      out.push_back(value);
      if (end == std::string_view::npos) break;
      text.remove_prefix(end + 1);
    }
    return out;
  }
  static Input key(std::string name, int modifier = 1) {
    if (modifier >= 1 && modifier <= 8) {
      if ((modifier - 1) & 4) name = "CTRL_" + name;
      else if ((modifier - 1) & 2) name = "ALT_" + name;
      else if ((modifier - 1) & 1 && name == "TAB") name = "SHIFT_TAB";
    }
    return {std::move(name)};
  }
  static Input codepoint(int code, int modifier) {
    if (code == 13) return key("ENTER", modifier);
    if (code == 9) return key("TAB", modifier);
    if (code == 27) return {"ESC"};
    if (code == 127) return key("BACKSPACE", modifier);
    if (code >= 32 && code < 127) {
      if ((modifier - 1) & 6)
        return key(std::string(1, char(std::toupper(static_cast<unsigned char>(code)))), modifier);
      return {"", std::string(1, char(code))};
    }
    return {};
  }
  static Input csi(std::string_view sequence) {
    char final = sequence.back();
    auto body = sequence.substr(0, sequence.size() - 1);
    if (body.starts_with('<')) {
      auto values = numbers(body.substr(1));
      if (values.size() != 3 || final != 'M' || values[1] < 1 || values[2] < 1)
        return {};
      int button = values[0];
      if (button & 32) return {}; // motion reports are not clicks
      if (!(button & 64) && (button & 3) != 0) return {};
      return {"", "", values[1] - 1, values[2] - 1,
              (button & 64) ? ((button & 1) ? -1 : 1) : 0, true};
    }
    auto values = numbers(body);
    int modifier = values.size() > 1 ? values[1] : 1;
    switch (final) {
    case 'A': return key("UP", modifier);
    case 'B': return key("DOWN", modifier);
    case 'C': return key("RIGHT", modifier);
    case 'D': return key("LEFT", modifier);
    case 'H': return key("HOME", modifier);
    case 'F': return key("END", modifier);
    case 'Z': return {"SHIFT_TAB"};
    case 'P': return key("F1", modifier);
    case 'Q': return key("F2", modifier);
    case 'R': return key("F3", modifier);
    case 'S': return key("F4", modifier);
    case 'u': return values.empty() ? Input{} : codepoint(values[0], modifier);
    case '~': break;
    default: return {};
    }
    if (values.empty()) return {};
    if (values.size() == 3 && values[0] == 27) return codepoint(values[2], values[1]);
    switch (values[0]) {
    case 1: case 7: return key("HOME", modifier);
    case 3: return key("DELETE", modifier);
    case 4: case 8: return key("END", modifier);
    case 5: return key("PGUP", modifier);
    case 6: return key("PGDN", modifier);
    case 11: return key("F1", modifier);
    case 12: return key("F2", modifier);
    case 13: return key("F3", modifier);
    case 14: return key("F4", modifier);
    case 15: return key("F5", modifier);
    case 17: return key("F6", modifier);
    case 18: return key("F7", modifier);
    case 19: return key("F8", modifier);
    case 20: return key("F9", modifier);
    case 21: return key("F10", modifier);
    case 23: return key("F11", modifier);
    case 24: return key("F12", modifier);
    default: return {};
    }
  }
public:
  void feed(std::string_view value) {
    if (bytes_.size() + value.size() > 65536) throw std::runtime_error("Terminal input exceeds 64 KiB");
    bytes_.append(value);
  }
  std::optional<Input> next(bool flush_escape = false) {
    if (bytes_.empty()) return {};
    if (paste_) {
      auto end = bytes_.find("\x1b[201~");
      if (end == std::string::npos) return {};
      auto text = bytes_.substr(0, end);
      bytes_.erase(0, end + 6);
      paste_ = false;
      // Paste is text, including newlines, never an Enter/send action.
      std::string normalized;
      for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
          normalized += '\n';
          if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else normalized += text[i];
      }
      return Input{"", std::move(normalized)};
    }
    if (bytes_[0] == '\x1b') {
      if (bytes_.size() == 1) {
        if (!flush_escape) return {};
        bytes_.clear(); return Input{"ESC"};
      }
      if (bytes_[1] == '[') {
        size_t end = 2;
        while (end < bytes_.size() && !(bytes_[end] >= '@' && bytes_[end] <= '~')) ++end;
        if (end == bytes_.size()) {
          if (!flush_escape) return {};
          bytes_.erase(0, 1); return Input{"ESC"};
        }
        auto sequence = bytes_.substr(2, end - 1);
        bytes_.erase(0, end + 1);
        if (sequence == "200~") { paste_ = true; return next(flush_escape); }
        return csi(sequence);
      }
      if (bytes_[1] == 'O') {
        if (bytes_.size() < 3) {
          if (!flush_escape) return {};
          bytes_.erase(0, 1); return Input{"ESC"};
        }
        auto sequence = bytes_.substr(2, 1);
        bytes_.erase(0, 3); return csi(sequence);
      }
      auto value = static_cast<unsigned char>(bytes_[1]);
      bytes_.erase(0, 2);
      if (value == '\r' || value == '\n') return Input{"ALT_ENTER"};
      if (value >= 32 && value < 127) return key(std::string(1, char(std::toupper(value))), 3);
      return Input{};
    }
    auto first = static_cast<unsigned char>(bytes_[0]);
    if (first < 32 || first == 127) {
      bytes_.erase(0, 1);
      if (first == '\r' || first == '\n') return Input{"ENTER"};
      if (first == '\t') return Input{"TAB"};
      if (first == 127 || first == 8) return Input{"BACKSPACE"};
      if (first > 0 && first < 27) return Input{"CTRL_" + std::string(1, char('A' + first - 1))};
      return Input{};
    }
    size_t length = first < 128 ? 1 : (first & 224) == 192 ? 2 : (first & 240) == 224 ? 3 : (first & 248) == 240 ? 4 : 1;
    if (bytes_.size() < length) return {};
    for (size_t i = 1; i < length; ++i) {
      if ((static_cast<unsigned char>(bytes_[i]) & 192) != 128) { length = 1; break; }
    }
    auto text = bytes_.substr(0, length);
    bytes_.erase(0, length);
    return Input{"", std::move(text)};
  }
};
}
