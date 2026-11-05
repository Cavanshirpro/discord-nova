#include "terminal_input.hpp"
#include <iostream>

static void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
int main() {
  try {
    nova::TerminalInput input;
    input.feed("\x1b[");
    require(!input.next(), "Fragmented CSI must wait");
    input.feed("1;5A");
    require(input.next()->key == "CTRL_UP", "Modified arrow");
    input.feed("\x1b[3;5~\x1bOP\x1b[17~\x1b[Z\x1bm\x1b\r");
    for (auto name : {"CTRL_DELETE", "F1", "F6", "SHIFT_TAB", "ALT_M", "ALT_ENTER"})
      require(input.next()->key == name, "Function/Alt key decoding");
    input.feed("\xC9");
    require(!input.next(), "Incomplete UTF-8 must wait");
    input.feed("\x99");
    require(input.next()->text == "ə", "Azeri UTF-8 corruption");
    input.feed("\xF0\x9F");
    require(!input.next(), "Incomplete emoji must wait");
    input.feed("\x8C\x9F");
    require(input.next()->text == "🌟", "Emoji corruption");
    input.feed("\x1b[200~Türkçə\r\nmessage\n\x11");
    require(!input.next(), "Incomplete paste must wait");
    input.feed("\x1b[201~");
    auto paste = input.next();
    require(paste && paste->key.empty() && paste->text == "Türkçə\nmessage\n\x11", "Paste must remain one text event, never send/quit");
    input.feed("\x1b[<0;20;10M\x1b[<64;20;10M\x1b[<65;20;10M");
    auto mouse = input.next();
    require(mouse->mouse && mouse->x == 19 && mouse->y == 9 && mouse->wheel == 0, "SGR mouse coordinates");
    require(input.next()->wheel == 1 && input.next()->wheel == -1, "Mouse wheel direction");
    input.feed("\x1b[49;5u\x1b[27;5;13~\x1b");
    require(input.next()->key == "CTRL_1", "Extended Ctrl+digit");
    require(input.next()->key == "CTRL_ENTER", "Extended Ctrl+Enter");
    require(!input.next(), "Escape must allow a following sequence");
    require(input.next(true)->key == "ESC", "Standalone Escape timeout");
    input.feed("\x1b[");
    require(input.next(true)->key == "ESC" && input.next()->text == "[", "Incomplete escape recovery");
    bool bounded = false;
    try { input.feed(std::string(65537, 'x')); } catch (const std::runtime_error&) { bounded = true; }
    require(bounded, "Input memory must be bounded");
    std::cout << "Terminal UTF-8, fragmented keys, paste, mouse and modifiers passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
