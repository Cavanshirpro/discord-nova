#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif
#include "app.hpp"
#include "media_config.hpp"
#include "terminal_input.hpp"
#include <nova/core.hpp>
#include <atomic>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
namespace {
using namespace nova;
std::atomic_bool interrupted = false;
#ifdef _WIN32
BOOL WINAPI signal_handler(DWORD event) {
  if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT ||
      event == CTRL_CLOSE_EVENT) {
    interrupted = true;
    return TRUE;
  }
  return FALSE;
}
std::string utf8(std::wstring value) {
  if (value.empty())
    return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()),
                                 nullptr, 0, nullptr, nullptr);
  std::string out(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), out.data(),
                      size, nullptr, nullptr);
  return out;
}
std::string clipboard() {
  if (!OpenClipboard(nullptr))
    return {};
  std::wstring value;
  auto data = GetClipboardData(CF_UNICODETEXT);
  if (data) {
    auto ptr = static_cast<const wchar_t *>(GlobalLock(data));
    if (ptr) {
      size_t n = 0;
      while (n < 16000 && ptr[n])
        ++n;
      value.assign(ptr, n);
      GlobalUnlock(data);
    }
  }
  CloseClipboard();
  return utf8(value);
}
#endif
class Terminal {
#ifdef _WIN32
  DWORD output_mode_ = 0, input_mode_ = 0;
  UINT input_cp_ = 0, output_cp_ = 0;
  wchar_t high_ = 0;
#else
  termios original_{};
  TerminalInput input_;
  std::chrono::steady_clock::time_point last_input_{};
#endif
public:
  Terminal() {
#ifdef _WIN32
    if (!GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode_) ||
        !GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &input_mode_))
      throw std::runtime_error("NOVA etkileşimli bir terminal gerektiriyor.");
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE),
                   output_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleMode(
        GetStdHandle(STD_INPUT_HANDLE),
        (input_mode_ | ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT |
         ENABLE_MOUSE_INPUT) &
            ~(ENABLE_QUICK_EDIT_MODE | ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT));
    input_cp_ = GetConsoleCP();
    output_cp_ = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(signal_handler, TRUE);
    SetConsoleTitleW(L"NOVA · Discord C++ · Ctrl+Q ile çık");
    DWORD processes[8];
    if (GetConsoleProcessList(processes, 8) == 1) {
      auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
      auto largest = GetLargestConsoleWindowSize(handle);
      CONSOLE_SCREEN_BUFFER_INFO info{};
      if (GetConsoleScreenBufferInfo(handle, &info)) {
        SHORT w = std::min<SHORT>(150, largest.X),
              h = std::min<SHORT>(42, largest.Y);
        COORD buffer{std::max(w, info.dwSize.X), std::max(h, info.dwSize.Y)};
        SetConsoleScreenBufferSize(handle, buffer);
        SMALL_RECT rect{0, 0, SHORT(w - 1), SHORT(h - 1)};
        SetConsoleWindowInfo(handle, TRUE, &rect);
      }
    }
#else
    if (!isatty(0) || !isatty(1))
      throw std::runtime_error("NOVA etkileşimli bir terminal gerektiriyor.");
    if (tcgetattr(0, &original_) != 0) throw std::runtime_error("Cannot read terminal settings");
    auto raw = original_;
    raw.c_lflag &= ~(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | INLCR | IGNCR | ISTRIP);
    raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &raw) != 0) throw std::runtime_error("Cannot configure terminal");
    std::signal(SIGINT, [](int) { interrupted = true; });
    std::signal(SIGTERM, [](int) { interrupted = true; });
    std::cout << "\x1b[?1000h\x1b[?1006h\x1b[?2004h";
#endif
    std::cout << "\x1b[?1049h\x1b[?25l\x1b[?7l" << std::flush;
  }
  ~Terminal() {
    std::cout << "\x1b[0m\x1b[?25h\x1b[?7h\x1b[?1049l" << std::flush;
#ifdef _WIN32
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), output_mode_);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), input_mode_);
    SetConsoleCP(input_cp_);
    SetConsoleOutputCP(output_cp_);
    SetConsoleCtrlHandler(signal_handler, FALSE);
#else
    std::cout << "\x1b[?1000l\x1b[?1006l\x1b[?2004l" << std::flush;
    tcsetattr(0, TCSANOW, &original_);
#endif
  }
  std::pair<int, int> size() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
      return {info.srWindow.Right - info.srWindow.Left + 1,
              info.srWindow.Bottom - info.srWindow.Top + 1};
#else
    winsize size{};
    if (!ioctl(1, TIOCGWINSZ, &size))
      return {size.ws_col, size.ws_row};
#endif
    return {120, 36};
  }
  std::optional<Input> read() {
#ifdef _WIN32
    for (int skipped = 0; skipped < 256; ++skipped) {
      auto handle = GetStdHandle(STD_INPUT_HANDLE);
      DWORD count = 0;
      if (!GetNumberOfConsoleInputEvents(handle, &count) || !count)
        return {};
      INPUT_RECORD record{};
      DWORD read = 0;
      ReadConsoleInputW(handle, &record, 1, &read);
      if (!read)
        return {};
      if (record.EventType == WINDOW_BUFFER_SIZE_EVENT)
        return Input{"", "", 0, 0, 0, false, true};
      if (record.EventType == MOUSE_EVENT) {
        auto &m = record.Event.MouseEvent;
        CONSOLE_SCREEN_BUFFER_INFO info{};
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info);
        if (m.dwEventFlags == MOUSE_WHEELED)
          return Input{"",
                       "",
                       m.dwMousePosition.X - info.srWindow.Left,
                       m.dwMousePosition.Y - info.srWindow.Top,
                       SHORT(HIWORD(m.dwButtonState)) > 0 ? 1 : -1,
                       true};
        if ((m.dwEventFlags == 0 || m.dwEventFlags == DOUBLE_CLICK) &&
            (m.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED))
          return Input{"",
                       "",
                       m.dwMousePosition.X - info.srWindow.Left,
                       m.dwMousePosition.Y - info.srWindow.Top,
                       0,
                       true};
        continue;
      }
      if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
        continue;
      auto &k = record.Event.KeyEvent;
      bool ctrl =
               k.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED),
           alt = k.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED),
           shift = k.dwControlKeyState & SHIFT_PRESSED;
      std::string special;
      switch (k.wVirtualKeyCode) {
      case VK_RETURN:
        special = "ENTER";
        break;
      case VK_ESCAPE:
        special = "ESC";
        break;
      case VK_TAB:
        special = shift ? "SHIFT_TAB" : "TAB";
        break;
      case VK_UP:
        special = "UP";
        break;
      case VK_DOWN:
        special = "DOWN";
        break;
      case VK_LEFT:
        special = "LEFT";
        break;
      case VK_RIGHT:
        special = "RIGHT";
        break;
      case VK_HOME:
        special = "HOME";
        break;
      case VK_END:
        special = "END";
        break;
      case VK_PRIOR:
        special = "PGUP";
        break;
      case VK_NEXT:
        special = "PGDN";
        break;
      case VK_BACK:
        special = "BACKSPACE";
        break;
      case VK_DELETE:
        special = "DELETE";
        break;
      default:
        if (k.wVirtualKeyCode >= VK_F1 && k.wVirtualKeyCode <= VK_F12)
          special = "F" + std::to_string(k.wVirtualKeyCode - VK_F1 + 1);
      }
      if (ctrl && !alt) {
        if (k.wVirtualKeyCode == 'V')
          return Input{"", clipboard()};
        if (special.empty() && k.wVirtualKeyCode >= '0' &&
            k.wVirtualKeyCode <= 'Z')
          special = char(k.wVirtualKeyCode);
        if (!special.empty())
          return Input{"CTRL_" + special};
      }
      if (alt && !ctrl) {
        if (special.empty() && k.wVirtualKeyCode >= '0' &&
            k.wVirtualKeyCode <= 'Z')
          special = char(k.wVirtualKeyCode);
        if (!special.empty())
          return Input{"ALT_" + special};
      }
      if (!special.empty())
        return Input{special};
      wchar_t wc = k.uChar.UnicodeChar;
      if (wc < 32)
        continue;
      if (wc >= 0xD800 && wc <= 0xDBFF) {
        high_ = wc;
        continue;
      }
      std::wstring text;
      if (high_) {
        if (wc >= 0xDC00 && wc <= 0xDFFF)
          text.push_back(high_);
        high_ = 0;
      }
      text.push_back(wc);
      auto value = utf8(text), out = value;
      for (int i = 1; i < std::min<int>(k.wRepeatCount, 40); ++i)
        out += value;
      return Input{"", out};
    }
    return {};
#else
    fd_set ready;
    FD_ZERO(&ready);
    FD_SET(0, &ready);
    timeval timeout{};
    if (select(1, &ready, nullptr, nullptr, &timeout) > 0) {
      char buffer[4096];
      auto count = ::read(0, buffer, sizeof(buffer));
      if (count > 0) {
        input_.feed(std::string_view(buffer, size_t(count)));
        last_input_ = std::chrono::steady_clock::now();
      }
    }
    return input_.next(std::chrono::steady_clock::now() - last_input_ > std::chrono::milliseconds(60));
#endif
  }
};
Json offline_config() {
  return {{"guild_id", "400"},
          {"channel_id", "202"},
          {"camera", false},
          {"go_live", false},
          {"play_received_audio", false}};
}
void fixture(std::string url, std::string profile = {}, std::string ca_bundle = {}) {
  discord::ClientOptions options;
  options.api_url = url;
  options.request_timeout = std::chrono::seconds(3);
  options.browser_profile = std::move(profile);
  options.ca_bundle = std::move(ca_bundle);
  Backend backend(offline_config(), "offline-token", "recordings", options);
  backend.start(false);
  std::vector<Json> backlog;
  auto await = [&](const std::string &type, uint64_t request = 0) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < until) {
      auto fresh = backend.drain();
      for (auto &e : fresh)
        backlog.push_back(std::move(e));
      for (auto it = backlog.begin(); it != backlog.end(); ++it)
        if (str(*it, "type") == type &&
            (!request || it->value("request", uint64_t{}) == request)) {
          auto e = *it;
          backlog.erase(it);
          return e;
        }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Fixture event timeout: " + type);
  };
  await("me");
  await("guilds");
  await("dms");
  await("friends");
  backend.channels("400");
  await("channels");
  backend.history("200");
  await("history");
  backend.send("200", "fixture message", "", 101);
  await("sent", 101);
  backend.send("200", "fixture reply", "900", 102);
  await("sent", 102);
  backend.edit("200", "900", "edited", 103);
  await("sent", 103);
  backend.edit("200", "901", "forbidden", 104);
  await("error", 104);
  backend.remove("200", "901");
  await("error");
  backend.react("200", "900", "👍");
  await("notice");
  backend.remove("200", "900");
  await("message_delete");
  await("notice");
  backend.dm("101");
  await("dm_open");
  backend.profile("101", "");
  await("profile");
  backend.media("mode", {{"camera", false}, {"go_live", false}});
  auto preferences = await("media_preferences");
  if (preferences["data"]["camera"] != false ||
      preferences["data"]["go_live"] != false)
    throw std::runtime_error("Pre-join output preferences were lost");
  backend.send("200", "fixture restricted", "", 105);
  auto restricted = await("error", 105);
  if (!restricted.value("writes_blocked", false) ||
      restricted.value("code", 0) != 40004)
    throw std::runtime_error("Account restriction was not retained");
  backend.send("200", "fixture must stay blocked", "", 106);
  await("error", 106);
  backend.dm("101");
  await("error");
  backend.stop();
  auto shutdown_options = options;
  shutdown_options.gateway_url = url;
  shutdown_options.gateway_url.replace(0, url.starts_with("https:") ? 5 : 4,
                                       url.starts_with("https:") ? "wss" : "ws");
  shutdown_options.gateway_url += "/gateway";
  shutdown_options.max_reconnect_attempts = 0;
  for (int i = 0; i < 12; ++i) {
    Backend closing(offline_config(), "offline-token", "recordings", shutdown_options);
    closing.start();
    closing.stop();
  }
  std::cout << "REST fixture passed: history, reply, edits, ownership, "
               "reaction, DM, profile, account-restriction and immediate shutdown\n";
}
Input scripted(std::string key) {
  if (key == "\r" || key == "\n")
    return {"ENTER"};
  if (key == "\x1b")
    return {"ESC"};
  if (key.size() == 1)
    return {"", key};
  return {key};
}
int execute(std::vector<std::filesystem::path> args) {
  try {
    auto transport = nova::frontend_options().transport;
    for (size_t i = 1; i < args.size();) {
      if (args[i] == "--browser-profile" || args[i] == "--ca-bundle") {
        const bool profile = args[i] == "--browser-profile";
        if (i + 1 == args.size()) throw std::invalid_argument("Missing connection option value");
        auto value = media_example::text(args[i + 1]);
        if (profile) transport.browser_profile = value == "default" ? "" : value;
        else transport.ca_bundle = value;
        args.erase(args.begin() + i, args.begin() + i + 2);
      } else ++i;
    }
    if (args.size() == 2 && args[1] == "--version") {
      std::cout << "discord-nova " NOVA_VERSION "\n";
      return 0;
    }
    if (args.size() == 2 && args[1] == "--capabilities") {
      std::cout << Json{{"browser_profiles", discord::supports_browser_impersonation()},
                        {"native_media", discord::MediaSession::available()}}.dump() << '\n';
      return 0;
    }
    if (args.size() == 2 && (args[1] == "--help" || args[1] == "-h")) {
      std::cout << "discord-nova-tui CONFIG TOKEN_FILE [EVENT_LOG]\n"
                   "discord-nova-tui --demo | --self-test | --version | --capabilities\n"
                   "Connection: --browser-profile chrome152|default --ca-bundle ROOTS.pem\n"
                   "Ctrl+Q: exit; F1: shortcuts; Ctrl+K: search; F4: media; F6: friends.\n";
      return 0;
    }
    if (std::find(args.begin(), args.end(), "--smoke-test") != args.end()) {
      App app(nullptr, offline_config(), {}, true);
      app.seed_demo();
      if (app.render(150, 42).empty()) throw std::runtime_error("Empty TUI frame");
      std::cout << "Original Nova TUI demo rendered at 150x42\n";
      return 0;
    }
    if (args.size() > 1 && args[1] == "--self-test") {
      App::self_test();
      std::cout << "NOVA model/editor/layout self-tests passed\n";
      return 0;
    }
    if ((args.size() == 3 || args.size() == 5) && args[1] == "--fixture") {
      fixture(args[2].string(), args.size() == 5 ? args[3].string() : "",
              args.size() == 5 ? media_example::text(args[4]) : "");
      return 0;
    }
    if (args.size() >= 3 && args[1] == "--preview") {
      App app(nullptr, offline_config(), {}, true);
      app.seed_demo();
      int w = args.size() > 3 ? std::stoi(args[3].string()) : 150,
          h = args.size() > 4 ? std::stoi(args[4].string()) : 42;
      app.set_view(args.size() > 5 ? std::stoi(args[5].string()) : 0);
      if (args.size() > 6) {
        if (args[6] == "palette")
          app.input({"CTRL_K"});
        else if (args[6] == "help")
          app.input({"F1"});
        else if (args[6] == "compose") {
          app.input({"ENTER"});
          app.input({"", "Güzel görünüyor. Buradan devam edelim!"});
        } else if (args[6] == "profile") {
          app.set_view(2);
          app.input({"ENTER"});
        }
      }
      std::ofstream f(args[2], std::ios::binary);
      f << app.render(w, h);
      return f ? 0 : 1;
    }
    if (args.size() == 5 && args[1] == "--smoke") {
      std::ifstream f(args[2]);
      auto config = Json::parse(f);
      auto options = transport;
      options.request_timeout = std::chrono::seconds(10);
      Backend backend(config, media_example::token(args[3]),
                      args[2].parent_path() / "recordings", options);
      App app(&backend, config);
      backend.start();
      auto start = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - start <
             std::chrono::seconds(15)) {
        for (const auto &e : backend.drain())
          app.apply(e);
        app.tick();
        auto summary = app.summary();
        if (summary.value("live", false) &&
            summary.value("messages", size_t{}) > 0 &&
            std::chrono::steady_clock::now() - start > std::chrono::seconds(6))
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      auto report = app.summary();
      report["outgoing_message_tests"] = "offline fixture only";
      std::ofstream out(args[4]);
      out << report.dump(2);
      backend.stop();
      std::cout << report.dump() << '\n';
      return report.value("guilds", size_t{}) ? 0 : 1;
    }
    bool demo = args.size() == 2 && args[1] == "--demo";
    if (!demo && (args.size() < 3 || args.size() > 5)) {
      std::cerr
          << "Usage: discord-nova-tui CONFIG TOKEN_FILE [EVENT_LOG] "
             "[CONTROLS]\n       discord-nova-tui --demo | --help\n";
      return 2;
    }
#ifdef _WIN32
    HANDLE singleton = nullptr;
    if (!demo) {
      singleton =
          CreateMutexW(nullptr, FALSE, L"Local\\DiscordCppMyTestingNova");
      if (singleton && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleton);
        std::cerr << "NOVA zaten açık. Mevcut pencereyi kullan.\n";
        return 0;
      }
    }
    struct Release {
      HANDLE h;
      ~Release() {
        if (h)
          CloseHandle(h);
      }
    } release{singleton};
#endif
    Terminal terminal;
    Json config = offline_config();
    std::unique_ptr<Backend> backend;
    if (!demo) {
      std::ifstream f(args[1]);
      if (!f)
        throw std::runtime_error("Ayar dosyası açılamadı.");
      config = Json::parse(f);
      auto options = transport;
      options.request_timeout = std::chrono::seconds(10);
      options.max_rate_limit_wait = std::chrono::seconds(5);
      backend = std::make_unique<Backend>(
          config, media_example::token(args[2]),
          std::filesystem::absolute(args[1]).parent_path() / "recordings",
          options);
    }
    App app(backend.get(), config,
            demo ? std::filesystem::path{} : std::filesystem::absolute(args[1]),
            demo);
    if (demo)
      app.seed_demo();
    else
      backend->start();
    std::ofstream log;
    if (!demo && args.size() > 3)
      log.open(args[3], std::ios::app);
    Json script = Json::array();
    std::filesystem::file_time_type script_write{};
    size_t next_action = 0;
    if (args.size() == 5) {
      std::ifstream file(args[4]);
      script = Json::parse(file);
      script_write = std::filesystem::last_write_time(args[4]);
    }
    auto start = std::chrono::steady_clock::now();
    auto last_size = terminal.size();
    while (!app.quitting() && !interrupted) {
      if (backend)
        for (const auto &e : backend->drain()) {
          app.apply(e);
          if (log && str(e, "type") != "media" && str(e, "type") != "message" &&
              str(e, "type") != "typing" && str(e, "type") != "presence")
            log << Json{{"event", str(e, "type")},
                        {"time_unix_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count()},
                        {"operation", str(e, "operation")},
                        {"http_status", e.value("status", 0)},
                        {"discord_code", e.value("code", 0)},
                        {"gateway_code", e.value("gateway_code", 0)},
                        {"writes_blocked", e.value("writes_blocked", false)}}
                       .dump()
                << std::endl;
        }
      for (int n = 0; n < 100; ++n) {
        auto input = terminal.read();
        if (!input)
          break;
        app.input(*input);
      }
      if (args.size() == 5) {
        auto written = std::filesystem::last_write_time(args[4]);
        if (written != script_write) {
          try {
            std::ifstream file(args[4]);
            auto updated = Json::parse(file);
            if (updated.is_array() && updated.size() >= next_action) {
              script = std::move(updated);
              script_write = written;
            }
          } catch (const Json::exception &) {
          }
        }
        while (next_action < script.size() &&
               std::chrono::steady_clock::now() - start >=
                   std::chrono::milliseconds(
                       script[next_action].at("after_ms").get<int>())) {
          for (const auto &key : script[next_action].at("keys"))
            app.input(scripted(key.get<std::string>()));
          ++next_action;
        }
      }
      app.tick();
      auto size = terminal.size();
      bool resized = size != last_size;
      bool dirty = app.take_dirty();
      if (dirty || resized) {
        std::cout << app.render(size.first, size.second) << std::flush;
        last_size = size;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(dirty || resized ? 20 : 50));
    }
    app.apply({{"type", "notice"},
               {"message", "Bağlantılar kapatılıyor; taslaklar korunuyor…"}});
    auto size = terminal.size();
    std::cout << app.render(size.first, size.second) << std::flush;
    app.save();
    if (backend)
      backend->stop();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "NOVA: " << e.what() << '\n';
    return 1;
  }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) { return execute({argv, argv + argc}); }
#else
int main(int argc, char **argv) { return execute({argv, argv + argc}); }
#endif
