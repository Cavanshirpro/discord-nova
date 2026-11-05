#pragma once
#include "backend.hpp"
#include "text.hpp"
#include "input.hpp"
#include <chrono>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <utility>
namespace nova {
enum class View { Chat, Voice, Friends, Events, Help };
struct Draft {
  Editor edit;
  std::string reply, editing;
  std::string nonce, sent_content, sent_reply;
  bool pending = false;
  uint64_t request = 0;
};
class App {
  Backend *backend_;
  Json config_, me_ = Json::object(), guilds_ = Json::array(),
                dms_ = Json::array(), friends_ = Json::array(),
                voice_ = Json::object();
  std::map<std::string, Json> channels_, messages_;
  std::map<std::string, std::set<std::string>> deleted_;
  std::map<std::string, Draft> drafts_;
  std::map<std::string, int> unread_;
  std::map<std::string, std::string> presence_;
  std::map<std::string, std::chrono::steady_clock::time_point> typing_;
  std::deque<std::pair<std::string, std::string>> events_;
  std::filesystem::path config_path_, root_;
  std::string guild_, text_channel_, selected_message_,
      status_ = "Discord'a bağlanılıyor…", last_recording_;
  std::string prompt_kind_, prompt_title_, profile_user_, pending_guild_,
      filter_, friend_filter_;
  Json profile_ = Json::object();
  Editor prompt_;
  int focus_ = 1, guild_index_ = 0, channel_index_ = 0, friend_index_ = 0,
      scroll_ = 0, modal_index_ = 0;
  bool demo_ = false, live_ = false, media_active_ = false, composing_ = false,
       quit_ = false, error_ = false, dirty_ = true, settings_dirty_ = false,
       initial_ = true;
  bool help_open_ = false;
  bool writes_blocked_ = false;
  uint64_t request_ = 1;
  View view_ = View::Chat;
  std::chrono::steady_clock::time_point changed_ =
      std::chrono::steady_clock::now();
  struct Hit {
    Rect rect;
    std::string action;
    int index = 0;
    std::string value;
  };
  std::vector<Hit> hits_;
  std::vector<Json> navigation() const;
  std::vector<Json> palette() const;
  std::string guild_name() const;
  Json current_channel() const;
  Json selected_message() const;
  void note(std::string text, bool error = false);
  void mark();
  void choose_guild(int index);
  void choose_channel(const Json &channel);
  void choose_palette(const Json &entry);
  void merge(const Json &message, bool incoming = false);
  void ask(std::string kind, std::string title, std::string initial = {});
  void submit_prompt();
  void submit_message();
  void command(std::string action);
  void move_message(int delta);
  void draw_chat(Canvas &canvas, Rect area);
  void draw_voice(Canvas &canvas, Rect area);
  void draw_friends(Canvas &canvas, Rect area);
  void draw_help(Canvas &canvas, Rect area);
  void draw_modal(Canvas &canvas);
  void draw_sidebar(Canvas &canvas, Rect area);
  void media_action(std::string action, Json data = Json::object());

public:
  App(Backend *backend, Json config, std::filesystem::path config_path = {},
      bool demo = false);
  ~App();
  void apply(const Json &event);
  void input(const Input &input);
  std::string render(int width, int height);
  void seed_demo();
  void set_view(int view);
  bool quitting() const { return quit_; }
  bool take_dirty() { return std::exchange(dirty_, false); }
  void tick();
  void save();
  Json summary() const;
  static void self_test();
};
} // namespace nova
