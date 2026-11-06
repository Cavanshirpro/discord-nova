#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
// clang-format on
#endif
#include "app.hpp"
#include "media_config.hpp"
#include "platform.hpp"
#include <cmath>
#include <ctime>
#include <fstream>
#include <stdexcept>
namespace nova {
using Clock = std::chrono::steady_clock;
static bool text_type(int type) {
  return type == 0 || type == 1 || type == 3 || type == 5 || type == 10 ||
         type == 11 || type == 12;
}
static bool voice_type(int type) { return type == 2 || type == 13; }
static bool id_less(const Json &a, const Json &b) {
  auto x = str(a, "id"), y = str(b, "id");
  return x.size() != y.size() ? x.size() < y.size() : x < y;
}
static std::pair<std::string, std::string> local_time(std::string timestamp) {
  if (timestamp.size() < 19)
    return {"", ""};
  std::tm time{};
  std::istringstream in(timestamp.substr(0, 19));
  in >> std::get_time(&time, "%Y-%m-%dT%H:%M:%S");
  if (in.fail())
    return {timestamp.substr(0, 10), timestamp.substr(11, 5)};
#ifdef _WIN32
  auto utc = _mkgmtime(&time);
#else
  auto utc = timegm(&time);
#endif
  auto zone = timestamp.find_first_of("+-", 19);
  if (zone != std::string::npos && timestamp.size() >= zone + 6) {
    try {
      int offset = std::stoi(timestamp.substr(zone + 1, 2)) * 3600 +
                   std::stoi(timestamp.substr(zone + 4, 2)) * 60;
      utc -= timestamp[zone] == '+' ? offset : -offset;
    } catch (...) {
    }
  }
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &utc);
#else
  localtime_r(&utc, &local);
#endif
  std::ostringstream day, clock;
  day << std::put_time(&local, "%d.%m.%Y");
  clock << std::put_time(&local, "%H:%M");
  return {day.str(), clock.str()};
}
static std::string now_clock() {
  auto t = std::time(nullptr);
  std::tm v{};
#ifdef _WIN32
  localtime_s(&v, &t);
#else
  localtime_r(&t, &v);
#endif
  std::ostringstream out;
  out << std::put_time(&v, "%H:%M:%S");
  return out.str();
}
static void write_json(const std::filesystem::path &path, const Json &data) {
  auto temp = path;
  temp += ".tmp";
  {
    std::ofstream f(temp, std::ios::binary);
    f << data.dump(2) << '\n';
    if (!f)
      throw std::runtime_error("Ayarlar yazılamadı.");
  }
#ifdef _WIN32
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::runtime_error("Ayarlar kaydedilemedi.");
#else
  std::filesystem::rename(temp, path);
#endif
}
App::App(Backend *backend, Json config, std::filesystem::path path, bool demo)
    : backend_(backend), config_(std::move(config)),
      config_path_(std::move(path)), demo_(demo) {
  root_ = config_path_.empty() ? std::filesystem::path(".")
                               : config_path_.parent_path();
  guild_ = str(config_, "ui_guild_id", str(config_, "guild_id"));
  text_channel_ = str(config_, "ui_text_channel_id");
  if (!config_path_.empty())
    try {
      auto file = root_ / "drafts.json";
      if (std::filesystem::exists(file) &&
          std::filesystem::file_size(file) < 1024 * 1024) {
        std::ifstream f(file);
        auto d = Json::parse(f);
        for (const auto &[channel, value] : d.items())
          if (value.is_string())
            drafts_[channel].edit.set(value.get<std::string>());
      }
    } catch (...) {
      note("Eski taslaklar okunamadı.", true);
    }
}
App::~App() {
  try {
    save();
  } catch (...) {
  }
}
void App::mark() {
  dirty_ = true;
  settings_dirty_ = true;
  changed_ = Clock::now();
}
void App::note(std::string text, bool error) {
  status_ = clean(text);
  error_ = error;
  events_.push_front({now_clock(), status_});
  if (events_.size() > 200)
    events_.pop_back();
  dirty_ = true;
}
std::string App::guild_name() const {
  if (guild_.empty())
    return "Kişisel alan";
  for (const auto &g : guilds_)
    if (str(g, "id") == guild_)
      return str(g, "name");
  return "Sunucu";
}
Json App::current_channel() const {
  for (const auto &c : dms_)
    if (str(c, "id") == text_channel_)
      return c;
  for (const auto &[_, list] : channels_)
    for (const auto &c : list)
      if (str(c, "id") == text_channel_)
        return c;
  return Json{{"id", text_channel_},
              {"name", text_channel_.empty() ? "bir kanal seç" : text_channel_},
              {"type", 0}};
}
Json App::selected_message() const {
  auto list = messages_.find(text_channel_);
  if (list == messages_.end() || list->second.empty())
    return Json::object();
  if (!selected_message_.empty())
    for (const auto &m : list->second)
      if (str(m, "id") == selected_message_)
        return m;
  return list->second.back();
}
std::vector<Json> App::navigation() const {
  std::vector<Json> out;
  if (guild_.empty()) {
    for (const auto &c : dms_)
      out.push_back(c);
    return out;
  }
  auto found = channels_.find(guild_);
  if (found == channels_.end())
    return out;
  auto sorted = found->second.get<std::vector<Json>>();
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const Json &a, const Json &b) {
                     return a.value("position", 0) < b.value("position", 0);
                   });
  auto add_children = [&](const std::string &parent) {
    for (const auto &c : sorted)
      if (c.value("type", 0) != 4 && str(c, "parent_id") == parent)
        out.push_back(c);
  };
  add_children("");
  for (const auto &category : sorted)
    if (category.value("type", 0) == 4) {
      out.push_back(category);
      add_children(str(category, "id"));
    }
  for (const auto &c : sorted)
    if (c.value("type", 0) != 4 && !str(c, "parent_id").empty() &&
        std::none_of(out.begin(), out.end(), [&](const Json &o) {
          return str(o, "id") == str(c, "id");
        }))
      out.push_back(c);
  return out;
}
void App::choose_guild(int index) {
  guild_index_ = std::clamp(index, 0, int(guilds_.size()));
  guild_ = guild_index_ ? str(guilds_[size_t(guild_index_ - 1)], "id") : "";
  channel_index_ = 0;
  focus_ = 1;
  scroll_ = 0;
  selected_message_.clear();
  if (!guild_.empty() && !channels_.contains(guild_) && backend_)
    backend_->channels(guild_);
  auto nav = navigation();
  auto first = std::find_if(nav.begin(), nav.end(), [](const Json &c) {
    return text_type(c.value("type", 0));
  });
  if (first != nav.end())
    choose_channel(*first);
  else if (guild_.empty()) {
    text_channel_.clear();
    view_ = View::Friends;
  }
  mark();
}
void App::choose_channel(const Json &channel) {
  auto type = channel.value("type", 0);
  if (voice_type(type)) {
    view_ = View::Voice;
    if (backend_)
      backend_->join(str(channel, "guild_id", guild_), str(channel, "id"));
    else
      media_active_ = true;
    note("Ses kanalına bağlanılıyor: " + str(channel, "name"));
    dirty_ = true;
    return;
  }
  if (!text_type(type)) {
    note(type == 4 ? "Bir kategori içinden kanal seç."
                   : "Bu kanal türü henüz sohbet görünümünde desteklenmiyor.");
    return;
  }
  text_channel_ = str(channel, "id");
  if (messages_.contains(text_channel_))
    history_use_[text_channel_] = ++history_clock_;
  view_ = View::Chat;
  scroll_ = 0;
  selected_message_.clear();
  typing_.clear();
  unread_[text_channel_] = 0;
  composing_ = false;
  if (backend_)
    backend_->history(text_channel_);
  mark();
}
void App::merge(const Json &message, bool incoming) {
  auto channel = str(message, "channel_id"), id = str(message, "id");
  if (channel.empty() || id.empty() ||
      (deleted_.contains(channel) && deleted_.at(channel).contains(id)))
    return;
  if (incoming && channel != text_channel_) {
    if (unread_.contains(channel) || unread_.size() < 4096)
      unread_[channel] = std::min(9999, unread_[channel] + 1);
    if (!messages_.contains(channel))
      return;
  }
  auto &list = messages_[channel];
  if (!list.is_array())
    list = Json::array();
  auto found = std::lower_bound(list.begin(), list.end(), message, id_less);
  if (found == list.end() || str(*found, "id") != id)
    list.insert(found, message);
  else
    found->update(message);
  history_use_[channel] = ++history_clock_;
  while (list.size() > 500)
    list.erase(list.begin());
  if (messages_.size() > 12) {
    auto oldest = messages_.end();
    for (auto it = messages_.begin(); it != messages_.end(); ++it)
      if (it->first != text_channel_ && it->first != channel &&
          (oldest == messages_.end() ||
           history_use_.at(it->first) < history_use_.at(oldest->first)))
        oldest = it;
    if (oldest != messages_.end()) {
      deleted_.erase(oldest->first);
      history_use_.erase(oldest->first);
      messages_.erase(oldest);
    }
  }
  ++message_revision_;
  dirty_ = true;
}
void App::apply(const Json &e) {
  auto type = str(e, "type");
  dirty_ = true;
  if (type == "connection") {
    live_ = e.value("online", false);
    note(live_ ? "Canlı sohbet bağlantısı hazır."
               : "Canlı bağlantı kapalı. F5 ile görünümü yenileyebilirsin.",
         !live_);
  } else if (type == "me")
    me_ = e["user"];
  else if (type == "guilds") {
    guilds_ = e["items"];
    if (initial_) {
      initial_ = false;
      int selected = 0;
      for (size_t i = 0; i < guilds_.size(); ++i)
        if (str(guilds_[i], "id") == guild_)
          selected = int(i + 1);
      choose_guild(selected ? selected : guilds_.empty() ? 0 : 1);
    }
  } else if (type == "dms") {
    dms_ = e["items"];
    if (guild_.empty() && text_channel_.empty() && !dms_.empty())
      view_ = View::Friends;
  } else if (type == "friends")
    friends_ = e["items"];
  else if (type == "channels") {
    auto gid = str(e, "guild");
    channels_[gid] = e["items"];
    if (gid == guild_) {
      auto nav = navigation();
      bool chosen = false;
      for (size_t i = 0; i < nav.size(); ++i)
        if (str(nav[i], "id") == text_channel_ &&
            text_type(nav[i].value("type", 0))) {
          channel_index_ = int(i);
          choose_channel(nav[i]);
          chosen = true;
          break;
        }
      if (!chosen)
        for (size_t i = 0; i < nav.size(); ++i)
          if (text_type(nav[i].value("type", 0))) {
            channel_index_ = int(i);
            choose_channel(nav[i]);
            break;
          }
    }
  } else if (type == "history") {
    auto channel = str(e, "channel");
    history_errors_.erase(channel);
    if (e.value("older", false) && messages_.contains(channel)) {
      auto &list = messages_.at(channel);
      size_t additions = 0;
      for (const auto &m : e["items"])
        if (std::none_of(list.begin(), list.end(), [&](const Json &known) {
              return str(known, "id") == str(m, "id");
            }))
          ++additions;
      // Make room at the newer end when paging backwards. Dropping the oldest
      // rows here would silently discard the page the user just requested.
      while (!list.empty() && list.size() + additions > 500)
        list.erase(std::prev(list.end()));
      ++message_revision_;
    }
    for (const auto &m : e["items"])
      merge(m);
    if (e["items"].empty())
      note("Bu kanalda gösterilecek mesaj yok.");
  } else if (type == "message")
    merge(e["data"], true);
  else if (type == "message_edit") {
    ++message_revision_;
    auto channel = str(e, "channel"), id = str(e, "id");
    auto found = messages_.find(channel);
    if (found != messages_.end())
      for (auto &m : found->second)
        if (str(m, "id") == id) {
          m.update(e["patch"]);
          m["edited"] = true;
        }
  } else if (type == "message_delete") {
    auto channel = str(e, "channel"), id = str(e, "id");
    if (!messages_.contains(channel) && channel != text_channel_)
      return;
    ++message_revision_;
    deleted_[channel].insert(id);
    if (deleted_[channel].size() > 500)
      deleted_[channel].erase(deleted_[channel].begin());
    auto found = messages_.find(channel);
    if (found != messages_.end())
      std::erase_if(found->second.get_ref<Json::array_t &>(),
                    [&](const Json &m) { return str(m, "id") == id; });
    if (selected_message_ == id)
      selected_message_.clear();
  } else if (type == "sent") {
    merge(e["data"]);
    auto channel = str(e["data"], "channel_id");
    auto &d = drafts_[channel];
    if (d.request == e.value("request", uint64_t{})) {
      d.edit.set("");
      d.reply.clear();
      d.editing.clear();
      d.pending = false;
      d.nonce.clear();
      mark();
    }
    note(e["data"].value("edited", false) ? "Mesaj düzenlendi."
                                          : "Mesaj gönderildi.");
  } else if (type == "typing") {
    if (str(e["data"], "channel_id") == text_channel_ &&
        str(e["data"], "user_id") != str(me_, "id"))
      typing_[str(e["data"], "user_id")] = Clock::now();
  } else if (type == "presence") {
    auto id = str(e["data"].value("user", Json::object()), "id");
    if (!id.empty() && (presence_.contains(id) || presence_.size() < 4096))
      presence_[id] = str(e["data"], "status");
  } else if (type == "dm_open") {
    auto c = e["data"];
    if (std::none_of(dms_.begin(), dms_.end(), [&](const Json &o) {
          return str(o, "id") == str(c, "id");
        }))
      dms_.push_back(c);
    guild_.clear();
    guild_index_ = 0;
    prompt_kind_.clear();
    choose_channel(c);
    focus_ = 2;
    composing_ = true;
  } else if (type == "profile") {
    profile_ = e["data"];
    profile_user_ = str(profile_.value("user", Json::object()), "id");
    ask("profile", "Profil");
  } else if (type == "notice")
    note(str(e, "message"));
  else if (type == "error") {
    if (e.value("writes_blocked", false))
      writes_blocked_ = true;
    auto operation = str(e, "operation");
    if (operation.starts_with("history:")) {
      auto channel = operation.substr(8);
      auto message =
          e.value("status", 0) == 403
              ? "Bu kanalın mesajlarını okuma iznin yok. Başka bir kanal seç."
              : "Mesaj geçmişi yüklenemedi. F5 ile tekrar dene.";
      if (history_errors_.contains(channel) || history_errors_.size() < 64)
        history_errors_[channel] = message;
      if (channel != text_channel_)
        return;
      note(message, true);
      return;
    }
    auto request = e.value("request", uint64_t{});
    if (request)
      for (auto &[_, draft] : drafts_)
        if (draft.request == request)
          draft.pending = false;
    note(str(e, "message", "İşlem tamamlanamadı."), true);
  } else if (type == "media_preferences") {
    config_.update(e["data"]);
    if (e["data"].contains("muted"))
      voice_["self_mute"] = e["data"]["muted"];
    if (e["data"].contains("play_received_audio"))
      voice_["play_received_audio"] = e["data"]["play_received_audio"];
    mark();
    note("Tercih bir sonraki ses bağlantısına uygulanacak.");
  } else if (type == "media_stopped") {
    media_active_ = false;
    voice_["transports"] = Json::object();
    voice_["participants"] = Json::array();
    voice_["streams"] = Json::array();
    voice_["recording"] = false;
    voice_["audio_playback_active"] = false;
    note("Ses bağlantısı kapandı.");
  } else if (type == "media") {
    const auto &data = e["data"];
    auto event = str(data, "event");
    if (event == "snapshot") {
      voice_ = data;
      media_active_ = data.value("transports", Json::object())
                          .value("voice", Json::object())
                          .value("connected", false);
      for (auto key : {"guild_id", "channel_id", "video_source", "audio_source",
                       "play_received_audio", "voice_audio", "stream_audio",
                       "camera", "go_live"})
        if (data.contains(key) && config_[key] != data[key]) {
          config_[key] = data[key];
          mark();
        }
      if (data.contains("self_mute") &&
          config_.value("muted", false) != data["self_mute"]) {
        config_["muted"] = data["self_mute"];
        mark();
      }
    } else if (event == "recording_started") {
      last_recording_ = str(data, "directory");
      note("Kayıt başladı: " + last_recording_);
    } else if (event == "recording_finished")
      note("Kayıt tamamlandı. Alt+O ile klasörü aç.");
    else if (event == "playing")
      note("Ses kanalı bağlantısı hazır.");
    else if (event == "outputs_changed") {
      config_["camera"] = data.at("camera");
      config_["go_live"] = data.at("go_live");
      mark();
      note("Kamera/yayın tercihi bağlantı korunarak değiştirildi.");
    } else if (event == "sources_changed")
      note("Medya kaynakları değiştirildi.");
    else if (event.find("error") != std::string::npos)
      note(str(data, "message", event), true);
  }
}
std::vector<Json> App::palette() const {
  std::vector<Json> out;
  const std::vector<std::pair<std::string, std::string>> commands = {
      {"chat", "Sohbete dön"},
      {"voice", "Ses ve medya paneli"},
      {"friends", "Arkadaşlar"},
      {"record", "Kayıt başlat"},
      {"stop_record", "Kaydı bitir"},
      {"video", "Video dosyasını değiştir"},
      {"audio", "Ses dosyasını değiştir"},
      {"mute", "Gönderilen sesi sustur / aç"},
      {"deaf", "Gelen sesi kapat / aç"},
      {"playback", "Yerel sesi kapat / aç"},
      {"join", "ID ile ses kanalına katıl"},
      {"leave", "Ses kanalından ayrıl"},
      {"dm", "ID ile DM aç"},
      {"refresh", "Verileri yenile"},
      {"events", "Olay günlüğü"},
      {"help", "Kısayollar ve yardım"},
      {"quit", "Uygulamadan çık"}};
  auto query = folded(prompt_.text);
  for (const auto &[id, name] : commands)
    if (query.empty() || folded(name).find(query) != std::string::npos)
      out.push_back({{"kind", "command"},
                     {"action", id},
                     {"name", name},
                     {"detail", "Komut"}});
  for (size_t i = 0; i < guilds_.size(); ++i) {
    auto name = str(guilds_[i], "name");
    if (query.empty() || folded(name).find(query) != std::string::npos)
      out.push_back({{"kind", "guild"},
                     {"index", i + 1},
                     {"name", name},
                     {"detail", "Sunucu"}});
  }
  for (const auto &c : navigation())
    if (c.value("type", 0) != 4 &&
        (query.empty() ||
         folded(str(c, "name")).find(query) != std::string::npos))
      out.push_back({{"kind", "channel"},
                     {"data", c},
                     {"name", str(c, "name")},
                     {"detail", voice_type(c.value("type", 0)) ? "Ses kanalı"
                                                               : "Sohbet"}});
  return out;
}
void App::choose_palette(const Json &e) {
  auto kind = str(e, "kind");
  prompt_kind_.clear();
  if (kind == "command")
    command(str(e, "action"));
  else if (kind == "guild")
    choose_guild(e.at("index").get<int>());
  else
    choose_channel(e.at("data"));
}
void App::ask(std::string kind, std::string title, std::string initial) {
  prompt_kind_ = std::move(kind);
  prompt_title_ = std::move(title);
  prompt_.set(std::move(initial), true);
  modal_index_ = 0;
  dirty_ = true;
}
void App::media_action(std::string action, Json data) {
  if (backend_)
    backend_->media(std::move(action), std::move(data));
  else {
    if (action == "deaf")
      voice_["self_deaf"] = data.value("value", false);
    if (action == "mute")
      voice_["self_mute"] = data.value("value", false);
    if (action == "playback")
      voice_["play_received_audio"] = data.value("value", false);
    if (action == "record")
      voice_["recording"] = true;
    if (action == "stop_record")
      voice_["recording"] = false;
    if (action == "leave")
      media_active_ = false;
    voice_["audio_playback_active"] =
        media_active_ && voice_.value("play_received_audio", false) &&
        !voice_.value("self_deaf", false);
    note("Demo: medya işlemi simüle edildi; dosya yazılmadı.");
  }
}
void App::command(std::string action) {
  dirty_ = true;
  if (action == "quit") {
    quit_ = true;
    return;
  }
  if (action == "chat") {
    view_ = View::Chat;
    focus_ = 2;
  } else if (action == "voice")
    view_ = View::Voice;
  else if (action == "friends") {
    view_ = View::Friends;
    friend_index_ = 0;
    focus_ = 2;
  } else if (action == "events")
    view_ = View::Events;
  else if (action == "help") {
    help_open_ = !help_open_;
  } else if (action == "refresh") {
    if (backend_) {
      backend_->refresh();
      if (!guild_.empty())
        backend_->channels(guild_);
      if (!text_channel_.empty())
        backend_->history(text_channel_);
    }
    note("Görünüm yenileniyor…");
  } else if (action == "join")
    ask("guild", "Sunucu ID", str(config_, "guild_id"));
  else if (action == "dm")
    ask("dm", "DM açılacak kullanıcı ID");
  else if (action == "video")
    ask("video", "Video dosyası · F2 dosya seçer",
        str(config_, "video_source"));
  else if (action == "audio")
    ask("audio", "Ses dosyası · F2 dosya seçer", str(config_, "audio_source"));
  else if (action == "record")
    ask("record", "Kayıt süresi: ses video (saniye)", "20 10");
  else if (action == "stop_record")
    media_action("stop_record");
  else if (action == "leave")
    media_action("leave");
  else if (action == "mute")
    media_action(
        "mute",
        {{"value", !voice_.value("self_mute", config_.value("muted", false))}});
  else if (action == "deaf")
    media_action("deaf", {{"value", !voice_.value("self_deaf", false)}});
  else if (action == "playback")
    media_action("playback",
                 {{"value", !voice_.value(
                                "play_received_audio",
                                config_.value("play_received_audio", false))}});
  else if (action == "camera" || action == "stream") {
    if (str(config_, "video_source").empty()) {
      note("Önce bir video dosyası seç.", true);
      return;
    }
    config_[action == "camera" ? "camera" : "go_live"] =
        !config_.value(action == "camera" ? "camera" : "go_live", true);
    media_action("mode", {{"camera", config_.value("camera", true)},
                          {"go_live", config_.value("go_live", true)}});
    mark();
  } else if (action == "folder") {
    auto path = root_ / "recordings";
    std::filesystem::create_directories(path);
    if (!open_external(media_example::text(std::filesystem::absolute(path))))
      note("Kayıt klasörü açılamadı.", true);
  } else if (action == "reply" || action == "edit" || action == "delete" ||
             action == "react" || action == "profile" ||
             action == "open_message") {
    auto m = selected_message();
    if (m.empty()) {
      note("Önce sohbetten bir mesaj seç.");
      return;
    }
    auto author = m.value("author", Json::object());
    auto id = str(m, "id");
    auto &d = drafts_[text_channel_];
    if (action == "reply") {
      d.reply = id;
      d.editing.clear();
      focus_ = 2;
      composing_ = true;
    } else if (action == "edit" || action == "delete") {
      if (str(author, "id") != str(me_, "id")) {
        note("Bu işlem yalnızca kendi mesajlarında kullanılabilir.", true);
        return;
      }
      if (action == "edit") {
        d.edit.set(str(m, "content"));
        d.editing = id;
        d.reply.clear();
        composing_ = true;
        focus_ = 2;
      } else
        ask("delete", "Bu mesaj silinsin mi? Enter: sil · Esc: vazgeç", id);
    } else if (action == "react")
      ask("react", "Emoji ile tepki ver", "👍");
    else if (action == "profile") {
      profile_user_ = str(author, "id");
      if (backend_)
        backend_->profile(profile_user_, guild_);
    } else {
      auto c = current_channel();
      auto gid = str(c, "guild_id");
      auto url = "https://discord.com/channels/" + (gid.empty() ? "@me" : gid) +
                 "/" + text_channel_ + "/" + id;
      if (!open_external(url))
        note("Mesaj bağlantısı açılamadı.", true);
    }
  }
}
void App::submit_prompt() {
  auto kind = prompt_kind_, value = prompt_.text;
  try {
    if (kind == "palette") {
      auto list = palette();
      if (!list.empty())
        choose_palette(
            list[size_t(std::clamp(modal_index_, 0, int(list.size() - 1)))]);
      return;
    }
    if (kind == "profile") {
      if (backend_ && !profile_user_.empty())
        backend_->dm(profile_user_);
      prompt_kind_.clear();
      return;
    }
    if (kind == "guild") {
      pending_guild_ = discord::Snowflake(value).str();
      ask("channel", "Ses kanalı ID", str(config_, "channel_id"));
      return;
    }
    if (kind == "channel") {
      auto channel = discord::Snowflake(value).str();
      if (backend_)
        backend_->join(pending_guild_, channel);
      view_ = View::Voice;
    } else if (kind == "dm") {
      auto id = discord::Snowflake(value).str();
      if (backend_)
        backend_->dm(id);
    } else if (kind == "video" || kind == "audio") {
      if (value.size() > 1 && value.front() == '"' && value.back() == '"')
        value = value.substr(1, value.size() - 2);
      if (!value.empty() &&
          !std::filesystem::is_regular_file(media_example::path(value)))
        throw std::runtime_error("Dosya bulunamadı. F2 ile seçebilirsin.");
      std::string video =
                      kind == "video" ? value : str(config_, "video_source"),
                  audio =
                      kind == "audio" ? value : str(config_, "audio_source");
      media_action("sources", {{"video", video}, {"audio", audio}});
      if (!media_active_) {
        config_["video_source"] = video;
        config_["audio_source"] = audio;
        mark();
      }
    } else if (kind == "record") {
      std::istringstream in(value);
      int audio = 0, video = 0;
      std::string extra;
      if (!(in >> audio >> video) || (in >> extra) || audio < 0 || video < 0 ||
          audio > 300 || video > 300 || (!audio && !video))
        throw std::runtime_error(
            "Örnek: 20 10. Her süre 0–300 saniye olabilir.");
      auto name =
          "nova-" +
          std::to_string(
              std::chrono::system_clock::now().time_since_epoch().count());
      media_action("record",
                   {{"name", name}, {"audio", audio}, {"video", video}});
    } else if (kind == "delete") {
      if (backend_)
        backend_->remove(text_channel_, value);
      else
        apply({{"type", "message_delete"},
               {"channel", text_channel_},
               {"id", value}});
    } else if (kind == "react") {
      auto m = selected_message();
      if (backend_ && !m.empty())
        backend_->react(text_channel_, str(m, "id"), value);
      else
        note("Demo: tepki eklendi.");
    } else if (kind == "search") {
      if (view_ == View::Friends)
        friend_filter_ = value;
      else
        filter_ = value;
      scroll_ = 0;
    }
    prompt_kind_.clear();
    dirty_ = true;
  } catch (const std::exception &e) {
    note(e.what(), true);
  }
}
void App::submit_message() {
  if (writes_blocked_) {
    note("Hesap koruması: gönderim durduruldu. Discord uygulamasında hesabını "
         "kontrol et; taslağın korundu.",
         true);
    return;
  }
  if (text_channel_.empty()) {
    note("Önce bir yazı kanalı veya DM seç.", true);
    return;
  }
  auto &d = drafts_[text_channel_];
  if (d.pending || d.edit.text.empty())
    return;
  int units = 0;
  for (const auto &g : glyphs(d.edit.text, true))
    units += g.code > 0xFFFF ? 2 : 1;
  if (units > 2000) {
    note("Mesaj 2000 karakter sınırını aşıyor.", true);
    return;
  }
  d.pending = true;
  if (d.nonce.empty() || d.sent_content != d.edit.text ||
      d.sent_reply != d.reply) {
    d.nonce = discord::new_message_nonce();
    d.sent_content = d.edit.text;
    d.sent_reply = d.reply;
  }
  d.request = request_++;
  if (backend_) {
    if (d.editing.empty())
      backend_->send(text_channel_, d.edit.text, d.reply, d.request, d.nonce);
    else
      backend_->edit(text_channel_, d.editing, d.edit.text, d.request);
  } else {
    Json m{{"id",
            d.editing.empty() ? std::to_string(100000 + d.request) : d.editing},
           {"channel_id", text_channel_},
           {"author", me_},
           {"content", d.edit.text},
           {"timestamp", "2026-10-29T13:00:00Z"},
           {"edited", !d.editing.empty()}};
    apply({{"type", "sent"}, {"request", d.request}, {"data", m}});
  }
  mark();
}
void App::move_message(int delta) {
  auto found = messages_.find(text_channel_);
  if (found == messages_.end() || found->second.empty())
    return;
  auto &list = found->second;
  int at = int(list.size() - 1);
  for (size_t i = 0; i < list.size(); ++i)
    if (str(list[i], "id") == selected_message_)
      at = int(i);
  at = std::clamp(at + delta, 0, int(list.size() - 1));
  selected_message_ = str(list[size_t(at)], "id");
  scroll_ = std::max(0, int(list.size() - 1) - at) * 3;
  dirty_ = true;
}
void App::input(const Input &in) {
  dirty_ = true;
  if (in.resize)
    return;
  auto key = in.key;
  if (key == "CTRL_Q") {
    quit_ = true;
    return;
  }
  if (key == "CTRL_K") {
    ask("palette", "Git / komut ara");
    return;
  }
  if (in.mouse) {
    if (!prompt_kind_.empty()) {
      if (prompt_kind_ == "palette")
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
          if (it->action == "palette" && it->rect.has(in.x, in.y)) {
            auto list = palette();
            if (it->index < int(list.size()))
              choose_palette(list[size_t(it->index)]);
            break;
          }
      return;
    }
    if (help_open_)
      return;
    if (in.wheel) {
      if (focus_ == 2)
        scroll_ = std::max(0, scroll_ + (in.wheel > 0 ? 3 : -3));
      else
        channel_index_ = std::max(0, channel_index_ + (in.wheel > 0 ? -1 : 1));
      return;
    }
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
      if (it->rect.has(in.x, in.y)) {
        auto hit = *it;
        if (hit.action == "guild") {
          choose_guild(hit.index);
          return;
        }
        if (hit.action == "channel") {
          auto list = navigation();
          if (hit.index < int(list.size())) {
            channel_index_ = hit.index;
            focus_ = 1;
            choose_channel(list[size_t(hit.index)]);
          }
          return;
        }
        if (hit.action == "message") {
          selected_message_ = hit.value;
          focus_ = 2;
          composing_ = false;
          return;
        }
        if (hit.action == "compose") {
          focus_ = 2;
          composing_ = true;
          return;
        }
        if (hit.action == "watch") {
          media_action(hit.index ? "unwatch" : "watch", {{"key", hit.value}});
          return;
        }
        if (hit.action == "friend") {
          friend_index_ = hit.index;
          focus_ = 2;
          return;
        }
        command(hit.action);
        return;
      }
    return;
  }
  if (!prompt_kind_.empty()) {
    if (key == "ESC") {
      prompt_kind_.clear();
      return;
    }
    if (key == "ENTER") {
      submit_prompt();
      return;
    }
    if (key == "UP" || key == "DOWN") {
      modal_index_ = std::max(0, modal_index_ + (key == "UP" ? -1 : 1));
      return;
    }
    if (key == "F2" && (prompt_kind_ == "video" || prompt_kind_ == "audio")) {
#ifdef _WIN32
      wchar_t file[32768]{};
      OPENFILENAMEW dialog{};
      dialog.lStructSize = sizeof(dialog);
      dialog.hwndOwner = GetConsoleWindow();
      dialog.lpstrFile = file;
      dialog.nMaxFile = DWORD(std::size(file));
      dialog.lpstrFilter = L"Media "
                           L"files\0*.mp4;*.mkv;*.webm;*.mov;*.mp3;*.wav;*.ogg;"
                           L"*.flac;*.opus\0All files\0*.*\0";
      dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                     OFN_EXPLORER;
      if (GetOpenFileNameW(&dialog)) {
        prompt_.set(media_example::text(std::filesystem::path(file)));
        submit_prompt();
      }
#endif
      return;
    }
    if (prompt_kind_ == "delete" || prompt_kind_ == "profile")
      return;
    if (key == "BACKSPACE")
      prompt_.erase();
    else if (key == "DELETE")
      prompt_.del();
    else if (key == "LEFT")
      prompt_.left();
    else if (key == "RIGHT")
      prompt_.right();
    else if (key == "HOME")
      prompt_.cursor = 0;
    else if (key == "END")
      prompt_.cursor = prompt_.text.size();
    else if (key == "CTRL_U")
      prompt_.set("");
    else if (!in.text.empty())
      prompt_.insert(in.text);
    if (prompt_kind_ == "palette")
      modal_index_ = 0;
    return;
  }
  if (key == "F1") {
    help_open_ = !help_open_;
    return;
  }
  if (help_open_) {
    if (key == "ESC")
      help_open_ = false;
    return;
  }
  const std::map<std::string, std::string> shortcuts = {
      {"F4", "voice"},          {"F5", "refresh"},     {"F6", "friends"},
      {"F7", "events"},         {"CTRL_1", "chat"},    {"CTRL_2", "voice"},
      {"CTRL_3", "friends"},    {"CTRL_4", "events"},  {"ALT_M", "mute"},
      {"ALT_D", "deaf"},        {"ALT_L", "playback"}, {"ALT_R", "record"},
      {"ALT_S", "stop_record"}, {"ALT_V", "video"},    {"ALT_A", "audio"},
      {"ALT_J", "join"},        {"ALT_O", "folder"},   {"ALT_C", "camera"},
      {"ALT_G", "stream"}};
  if (auto s = shortcuts.find(key); s != shortcuts.end()) {
    command(s->second);
    return;
  }
  if (key == "TAB" || key == "SHIFT_TAB") {
    focus_ = (focus_ + (key == "TAB" ? 1 : 3)) % 4;
    composing_ = false;
    return;
  }
  if (key.starts_with("ALT_") && key.size() == 5 && key[4] >= '1' &&
      key[4] <= '9') {
    choose_guild(key[4] - '0');
    return;
  }
  if (key == "F3") {
    ask("search",
        view_ == View::Friends ? "Arkadaşlarda ara" : "Yüklü mesajlarda ara",
        view_ == View::Friends ? friend_filter_ : filter_);
    return;
  }
  if (key == "ESC") {
    if (composing_) {
      auto &d = drafts_[text_channel_];
      d.reply.clear();
      d.editing.clear();
      composing_ = false;
      mark();
    } else if (!filter_.empty()) {
      filter_.clear();
      scroll_ = 0;
    } else
      focus_ = 1;
    return;
  }
  if (focus_ == 0) {
    if (key == "UP")
      guild_index_ = std::max(0, guild_index_ - 1);
    else if (key == "DOWN")
      guild_index_ = std::min(int(guilds_.size()), guild_index_ + 1);
    else if (key == "ENTER")
      choose_guild(guild_index_);
    return;
  }
  if (focus_ == 1) {
    auto nav = navigation();
    if (key == "UP")
      channel_index_ = std::max(0, channel_index_ - 1);
    else if (key == "DOWN")
      channel_index_ =
          std::min(std::max(0, int(nav.size()) - 1), channel_index_ + 1);
    else if (key == "ENTER" && !nav.empty())
      choose_channel(
          nav[size_t(std::clamp(channel_index_, 0, int(nav.size() - 1)))]);
    return;
  }
  if (focus_ == 3) {
    if (key == "ENTER")
      command("voice");
    return;
  }
  if (view_ == View::Friends) {
    std::vector<Json> list;
    for (const auto &f : friends_)
      if (friend_filter_.empty() ||
          folded(str(f.value("user", Json::object()), "global_name",
                     str(f.value("user", Json::object()), "username")))
                  .find(folded(friend_filter_)) != std::string::npos)
        list.push_back(f);
    if (key == "UP")
      friend_index_ = std::max(0, friend_index_ - 1);
    else if (key == "DOWN")
      friend_index_ =
          std::min(std::max(0, int(list.size()) - 1), friend_index_ + 1);
    else if (!list.empty() && (key == "ENTER" || key == "CTRL_ENTER")) {
      auto id =
          str(list[size_t(std::clamp(friend_index_, 0, int(list.size() - 1)))],
              "id");
      if (backend_) {
        if (key == "CTRL_ENTER")
          backend_->dm(id);
        else
          backend_->profile(id, guild_);
      } else {
        profile_ = {{"user", list[size_t(friend_index_)]["user"]},
                    {"bio", "Burada yalnızca Discord'un bu hesaba gösterdiği "
                            "profil bilgileri yer alır."}};
        profile_user_ = id;
        ask("profile", "Profil");
      }
    }
    return;
  }
  if (view_ != View::Chat) {
    if (key == "PGUP" || key == "UP")
      scroll_ = std::max(0, scroll_ - 3);
    else if (key == "PGDN" || key == "DOWN")
      scroll_ += 3;
    return;
  }
  if (key == "CTRL_R") {
    command("reply");
    return;
  }
  if (key == "CTRL_E") {
    command("edit");
    return;
  }
  if (key == "CTRL_DELETE") {
    command("delete");
    return;
  }
  if (key == "CTRL_G") {
    command("react");
    return;
  }
  if (key == "CTRL_P") {
    command("profile");
    return;
  }
  if (key == "CTRL_O") {
    command("open_message");
    return;
  }
  if (key == "PGUP") {
    scroll_ += 8;
    composing_ = false;
    if (backend_ && messages_.contains(text_channel_) &&
        !messages_[text_channel_].empty())
      backend_->history(text_channel_,
                        str(messages_[text_channel_].front(), "id"));
    return;
  }
  if (key == "PGDN") {
    scroll_ = std::max(0, scroll_ - 8);
    return;
  }
  if (!composing_ && (key == "UP" || key == "DOWN")) {
    move_message(key == "UP" ? -1 : 1);
    return;
  }
  if (key == "ENTER") {
    if (composing_)
      submit_message();
    else
      composing_ = true;
    return;
  }
  if (text_channel_.empty())
    return;
  auto &d = drafts_[text_channel_];
  if (d.pending)
    return;
  if (key == "BACKSPACE")
    d.edit.erase();
  else if (key == "DELETE")
    d.edit.del();
  else if (key == "LEFT")
    d.edit.left();
  else if (key == "RIGHT")
    d.edit.right();
  else if (key == "HOME")
    d.edit.cursor = 0;
  else if (key == "END")
    d.edit.cursor = d.edit.text.size();
  else if (key == "CTRL_U")
    d.edit.set("");
  else if (key == "ALT_ENTER")
    d.edit.insert("\n");
  else if (!in.text.empty()) {
    d.edit.insert(in.text);
    composing_ = true;
  }
  mark();
}
void App::tick() {
  for (auto it = typing_.begin(); it != typing_.end();)
    if (Clock::now() - it->second > std::chrono::seconds(7)) {
      it = typing_.erase(it);
      dirty_ = true;
    } else
      ++it;
  if (settings_dirty_ && Clock::now() - changed_ > std::chrono::seconds(1))
    try {
      save();
    } catch (const std::exception &e) {
      settings_dirty_ = false;
      note(e.what(), true);
    }
}
void App::save() {
  if (config_path_.empty() || demo_ || !settings_dirty_)
    return;
  config_["ui_guild_id"] = guild_;
  config_["ui_text_channel_id"] = text_channel_;
  config_["auto_join_voice"] = false;
  write_json(config_path_, config_);
  Json drafts = Json::object();
  for (const auto &[channel, draft] : drafts_)
    if (!draft.edit.text.empty())
      drafts[channel] = draft.edit.text;
  write_json(root_ / "drafts.json", drafts);
  settings_dirty_ = false;
}
Json App::summary() const {
  return {{"demo", demo_},
          {"live", live_},
          {"guilds", guilds_.size()},
          {"channels", navigation().size()},
          {"dms", dms_.size()},
          {"friends", friends_.size()},
          {"selected_channel", text_channel_},
          {"messages", messages_.contains(text_channel_)
                           ? messages_.at(text_channel_).size()
                           : 0},
          {"voice_connected", media_active_},
          {"local_audio", voice_.value("audio_playback_active", false)},
          {"status", status_}};
}
void App::set_view(int v) {
  view_ = v == 1   ? View::Voice
          : v == 2 ? View::Friends
          : v == 3 ? View::Events
                   : View::Chat;
}
void App::draw_chat(Canvas &c, Rect a) {
  auto channel = current_channel();
  c.put(a.x + 2, a.y, "# " + str(channel, "name", "bir kanal seç"), White, Base,
        a.w - 4, true);
  c.put(a.x + 2, a.y + 1,
        fit(str(channel, "topic",
                text_channel_.empty()
                    ? "Sunucudan bir yazı kanalı seç veya arkadaşlarından bir "
                      "DM aç."
                    : "Canlı sohbet · F3 yerel arama · Ctrl+R yanıtla"),
            a.w - 4, true),
        Dim, Base, a.w - 4);
  c.line(a.x, a.y + 2, a.w);
  int compose_y = a.y + a.h - 5, body_y = a.y + 3,
      body_h = std::max(1, compose_y - body_y - 1);
  auto &lines = chat_lines_;
  if (layout_revision_ != message_revision_ || layout_width_ != a.w ||
      layout_channel_ != text_channel_ || layout_filter_ != filter_ ||
      layout_user_ != str(me_, "id")) {
    lines.clear();
    layout_revision_ = message_revision_;
    layout_width_ = a.w;
    layout_channel_ = text_channel_;
    layout_filter_ = filter_;
    layout_user_ = str(me_, "id");
    const auto folded_filter = folded(filter_);
    auto found = messages_.find(text_channel_);
    std::string day;
    if (found != messages_.end())
      for (const auto &m : found->second) {
        auto content = str(m, "content");
        auto author = m.value("author", Json::object());
        auto name =
            str(author, "global_name", str(author, "username", "Bilinmeyen"));
        if (!filter_.empty() &&
            folded(name + " " + content).find(folded_filter) ==
                std::string::npos)
          continue;
        auto clock = local_time(str(m, "timestamp"));
        if (clock.first != day) {
          day = clock.first;
          lines.push_back({"────────  " + day + "  ────────", "", Dim});
        }
        auto id = str(m, "id");
        auto label = fit(name, std::max(8, a.w - 20), true) + "  " +
                     clock.second +
                     (m.value("edited", false) ? "  (düzenlendi)" : "");
        lines.push_back(
            {label, id, str(author, "id") == str(me_, "id") ? Mint : Violet});
        if (m.contains("reply_preview"))
          lines.push_back(
              {"↳ " + fit(str(m, "reply_preview"), a.w - 7, true), id, Dim});
        else if (!str(m, "reply_to").empty())
          lines.push_back({"↳ bir mesaja yanıt", id, Dim});
        if (!content.empty())
          for (auto &row : wrap(content, std::max(1, a.w - 6), true))
            lines.push_back({row, id, Text});
        for (const auto &att : m.value("attachments", Json::array()))
          lines.push_back(
              {"[dosya] " + str(att, "filename") + " · " +
                   std::to_string(att.value("size", uint64_t{}) / 1024) + " KB",
               id, Accent});
        lines.push_back({"", id, Dim});
      }
  }
  if (lines.empty()) {
    int y = body_y + std::max(1, body_h / 3);
    c.put(a.x + 3, y, "Biraz daha sakin bir Discord.", White, Base, a.w - 6,
          true);
    c.put(a.x + 3, y + 2,
          history_errors_.contains(text_channel_)
              ? history_errors_.at(text_channel_)
          : text_channel_.empty() ? "Soldan bir kanal seçerek başla."
          : filter_.empty()       ? "Henüz mesaj yok veya geçmiş yükleniyor."
                                  : "Bu aramayla eşleşen yüklü mesaj yok.",
          Dim, Base, a.w - 6);
    c.put(a.x + 3, y + 4, "Ctrl+K: hızlı geçiş / komutlar", Accent, Base,
          a.w - 6);
  } else {
    int maximum = std::max(0, int(lines.size()) - body_h);
    scroll_ = std::clamp(scroll_, 0, maximum);
    int start = std::max(0, int(lines.size()) - body_h - scroll_);
    for (int row = 0; row < body_h && start + row < int(lines.size()); ++row) {
      auto &line = lines[size_t(start + row)];
      bool selected =
          !selected_message_.empty() && line.id == selected_message_;
      auto bg = selected ? Raised : Base;
      c.fill({a.x + 1, body_y + row, a.w - 2, 1}, bg);
      c.put(a.x + 3, body_y + row, line.text, line.tone, bg, a.w - 6,
            line.tone == Mint || line.tone == Violet);
      if (selected)
        c.put(a.x + 1, body_y + row, "▏", Accent, bg);
      if (!line.id.empty())
        hits_.push_back(
            {{a.x + 1, body_y + row, a.w - 2, 1}, "message", 0, line.id});
    }
  }
  if (history_errors_.contains(text_channel_))
    c.put(a.x + 2, compose_y - 1, history_errors_.at(text_channel_), Amber,
          Base, a.w - 4);
  else if (!filter_.empty())
    c.put(a.x + 2, compose_y - 1, "Arama: " + filter_ + " · Esc temizler",
          Amber, Base, a.w - 4);
  else if (scroll_)
    c.put(a.x + 2, compose_y - 1, "Geçmiştesin · PgDn ile en yeniye dön", Amber,
          Base, a.w - 4);
  else if (!typing_.empty())
    c.put(a.x + 2, compose_y - 1, "Birisi yazıyor…", Dim, Base, a.w - 4);
  auto &draft = drafts_[text_channel_];
  std::string label =
      draft.editing.empty()
          ? draft.reply.empty() ? "Mesaj" : "Yanıt yazılıyor · Esc iptal"
          : "Mesaj düzenleniyor · Esc iptal";
  c.box({a.x + 1, compose_y, a.w - 2, 4}, label,
        composing_ && focus_ == 2 ? Mint : Dim, Composer);
  auto input = draft.edit.text;
  auto display = wrap(input, std::max(1, a.w - 8));
  int cursor_line = 0;
  auto prior = wrap(input.substr(0, draft.edit.cursor), std::max(1, a.w - 8));
  cursor_line = int(prior.size()) - 1;
  int start = std::max(0, cursor_line - 1);
  if (input.empty())
    c.put(a.x + 3, compose_y + 1,
          text_channel_.empty() ? "Önce bir yazı kanalı seç."
                                : "Bir mesaj yaz…",
          Dim, Composer, a.w - 6);
  else
    for (int i = 0; i < 2 && start + i < int(display.size()); ++i)
      c.put(a.x + 3, compose_y + 1 + i, display[size_t(start + i)], Text,
            Composer, a.w - 6);
  if (composing_ && focus_ == 2 && !draft.pending) {
    int x = columns(prior.back());
    c.put(a.x + 3 + std::min(x, a.w - 7),
          compose_y + 1 + std::min(1, cursor_line - start), "▏", Mint,
          Composer);
  }
  c.put(a.x + 2, compose_y + 4,
        draft.pending ? "Gönderiliyor… Taslağın sonuç gelene kadar korunuyor."
                      : "Enter gönder · Alt+Enter yeni satır · Ctrl+R yanıt · "
                        "Ctrl+E düzenle",
        draft.pending ? Amber : Dim, Base, a.w - 4);
  hits_.push_back({{a.x + 1, compose_y, a.w - 2, 4}, "compose"});
}
void App::draw_voice(Canvas &c, Rect a) {
  c.put(a.x + 2, a.y, "SES ODASI", White, Base, a.w - 4, true);
  c.put(a.x + 2, a.y + 1,
        media_active_
            ? "Ses, kamera, yayın ve kayıt tek yerde."
            : "Soldan bir ses kanalına Enter ile katıl. Alt+J: ID ile katıl.",
        Dim, Base, a.w - 4);
  c.line(a.x, a.y + 2, a.w);
  int y = a.y + 4;
  int card_w = a.w - 4;
  auto button = [&](int x, int row, int w, std::string label,
                    std::string action, Tone tone = Accent) {
    c.fill({x, row, w, 1}, Raised);
    c.put(x + 1, row, label, tone, Raised, w - 2);
    hits_.push_back({{x, row, w, 1}, std::move(action)});
  };
  c.box({a.x + 2, y, card_w, 5}, "ÇIKIŞLAR", Accent, Raised);
  c.put(a.x + 4, y + 1,
        "Video  " +
            fit(str(config_, "video_source", "seçilmedi"), card_w - 12, true),
        Text, Raised, card_w - 4);
  c.put(a.x + 4, y + 2,
        "Ses    " +
            fit(str(config_, "audio_source", "seçilmedi"), card_w - 12, true),
        Text, Raised, card_w - 4);
  c.put(a.x + 4, y + 3, "Alt+V video seç · Alt+A ses seç · F2 dosya seçici",
        Dim, Raised, card_w - 4);
  y += 7;
  int button_w = std::max(10, (card_w - 2) / 2);
  button(a.x + 2, y, button_w,
         std::string(config_.value("camera", true) ? "● Kamera açık"
                                                   : "○ Kamera kapalı"),
         "camera", config_.value("camera", true) ? Mint : Dim);
  button(a.x + 3 + button_w, y, button_w,
         std::string(config_.value("go_live", true) ? "● Go Live açık"
                                                    : "○ Go Live kapalı"),
         "stream", config_.value("go_live", true) ? Violet : Dim);
  y += 2;
  if (y < a.y + a.h - 5) {
    bool recording = voice_.value("recording", false);
    c.box({a.x + 2, y, card_w, 5}, recording ? "KAYIT ETKİN" : "KAYIT",
          recording ? Red : Dim, Raised);
    c.put(a.x + 4, y + 1,
          recording ? "Gelen ses ve görüntüler kaydediliyor."
                    : "Kayıt kendiliğinden başlamaz.",
          recording ? Red : Text, Raised, card_w - 4);
    c.put(a.x + 4, y + 2, "Alt+R: 20 sn ses + 10 sn görüntü · Alt+S: bitir",
          Dim, Raised, card_w - 4);
    c.put(a.x + 4, y + 3, "Alt+O: kayıt klasörünü aç", Accent, Raised,
          card_w - 4);
    y += 7;
  }
  if (y < a.y + a.h - 3) {
    double fps = 0;
    uint64_t rx = 0, tx = 0;
    auto transports = voice_.value("transports", Json::object());
    for (const auto &[_, t] : transports.items()) {
      auto dt = t.value("video_interval_mean_ms", 0.0);
      if (dt > 0)
        fps = std::max(fps, 1000 / dt);
      rx += t.value("received_frames", uint64_t{});
      tx += t.value("video_frames_sent", uint64_t{});
    }
    std::ostringstream metrics;
    metrics << std::fixed << std::setprecision(1) << fps
            << " FPS gönderim  ·  Alınan " << rx << "  ·  Gönderilen " << tx;
    c.put(a.x + 3, y, metrics.str(), Dim, Base, a.w - 6);
    c.put(a.x + 3, y + 2, "Yerel ses kapalıyken alım ve kayıt devam eder.",
          Mint, Base, a.w - 6);
  }
  if (a.h >= 14)
    button(a.x + 2, a.y + a.h - 2, std::min(card_w, 22), "Ses kanalından ayrıl",
           "leave", Red);
}
void App::draw_friends(Canvas &c, Rect a) {
  c.put(a.x + 2, a.y, "ARKADAŞLAR & BAĞLANTILAR", White, Base, a.w - 4, true);
  c.put(a.x + 2, a.y + 1,
        "Enter profil · Ctrl+Enter DM · F3 ara · Ctrl+K > DM aç", Dim, Base,
        a.w - 4);
  c.line(a.x, a.y + 2, a.w);
  std::vector<Json> list;
  for (const auto &f : friends_) {
    auto u = f.value("user", Json::object());
    if (friend_filter_.empty() ||
        folded(str(u, "global_name", str(u, "username")))
                .find(folded(friend_filter_)) != std::string::npos)
      list.push_back(f);
  }
  if (list.empty()) {
    c.put(a.x + 3, a.y + 5,
          friend_filter_.empty()
              ? "Henüz bağlantı bilgisi yok veya liste yükleniyor."
              : "Aramayla eşleşen bağlantı yok.",
          Dim, Base, a.w - 6);
    return;
  }
  friend_index_ = std::clamp(friend_index_, 0, int(list.size() - 1));
  int room = std::max(1, (a.h - 4) / 3),
      start = std::max(0, friend_index_ - room + 1);
  for (int i = 0; i < room && start + i < int(list.size()); ++i) {
    auto &f = list[size_t(start + i)];
    auto u = f.value("user", Json::object());
    auto id = str(f, "id");
    bool selected = start + i == friend_index_;
    auto bg = selected ? Selected : Base;
    int y = a.y + 4 + i * 3;
    c.fill({a.x + 1, y, a.w - 2, 2}, bg);
    c.put(a.x + 3, y,
          (selected ? "› " : "  ") + str(u, "global_name", str(u, "username")),
          selected ? White : Text, bg, a.w - 6, true);
    auto type = f.value("type", 0);
    std::string kind = type == 1   ? "Arkadaş"
                       : type == 2 ? "Engellendi"
                       : type == 3 ? "Gelen istek"
                       : type == 4 ? "Gönderilen istek"
                                   : "Diğer bağlantı";
    std::string presence =
        presence_.contains(id) ? presence_[id] : "durum bilinmiyor";
    c.put(a.x + 5, y + 1, kind + " · " + presence, Dim, bg, a.w - 8);
    hits_.push_back({{a.x + 1, y, a.w - 2, 2}, "friend", start + i});
  }
}
void App::draw_sidebar(Canvas &c, Rect a) {
  c.fill(a, Sidebar);
  int y = a.y + 1;
  c.put(a.x + 2, y, "SES KATILIMCILARI", Dim, Sidebar, a.w - 4, true);
  y += 2;
  auto participants = voice_.value("participants", Json::array());
  if (participants.empty()) {
    c.put(a.x + 2, y, "Ses bağlantısı yok", Dim, Sidebar, a.w - 4);
    y += 2;
  }
  for (const auto &p : participants) {
    if (y >= a.y + a.h - 11)
      break;
    auto name =
        str(p, "display_name",
            str(p, "global_name", str(p, "username", str(p, "user_id"))));
    c.put(a.x + 2, y, "●", p.value("self_mute", false) ? Dim : Mint, Sidebar);
    c.put(a.x + 4, y, name, Text, Sidebar, a.w - 6);
    ++y;
    std::string flags = p.value("camera", false) ? "CAM " : "";
    if (p.value("streaming", false))
      flags += "LIVE ";
    if (p.value("self_deaf", false))
      flags += "SAĞIR ";
    if (p.value("self_mute", false))
      flags += "SESSİZ";
    if (!flags.empty()) {
      c.put(a.x + 4, y, flags, Dim, Sidebar, a.w - 6);
      ++y;
    }
    ++y;
  }
  y = std::min(y + 1, a.y + a.h - 8);
  c.put(a.x + 2, y, "YAYINLAR", Dim, Sidebar, a.w - 4, true);
  y += 2;
  auto streams = voice_.value("streams", Json::array());
  if (streams.empty())
    c.put(a.x + 2, y, "Henüz yayın yok", Dim, Sidebar, a.w - 4);
  for (const auto &s : streams) {
    if (y + 2 >= a.y + a.h)
      break;
    bool own = str(s, "owner_id") == str(me_, "id"),
         watching = s.value("watching", false);
    auto owner = str(s, "owner_id");
    for (const auto &p : participants)
      if (str(p, "user_id") == owner)
        owner = str(p, "username", owner);
    auto count = s.value("viewer_count", Json(nullptr));
    c.put(a.x + 2, y,
          own        ? "SENİN YAYININ"
          : watching ? "İZLENİYOR"
                     : "YAYIN VAR",
          own        ? Violet
          : watching ? Mint
                     : Amber,
          Sidebar, a.w - 4, true);
    ++y;
    c.put(a.x + 2, y, owner, Text, Sidebar, a.w - 4);
    ++y;
    c.put(a.x + 2, y,
          "İzleyici: " + (count.is_null() ? "bilinmiyor" : count.dump()), Dim,
          Sidebar, a.w - 4);
    if (!own)
      hits_.push_back({{a.x + 1, y - 2, a.w - 2, 3},
                       "watch",
                       watching ? 1 : 0,
                       str(s, "key")});
    y += 2;
  }
}
void App::draw_help(Canvas &c, Rect a) {
  c.put(a.x + 2, a.y, "NOVA / KLAVYE KILAVUZU", White, Base, a.w - 4, true);
  c.put(a.x + 2, a.y + 1,
        "Tıklayabilir veya tüm arayüzü klavyeyle kullanabilirsin.", Dim, Base,
        a.w - 4);
  c.line(a.x, a.y + 2, a.w);
  const std::vector<std::pair<std::string, std::string>> help = {
      {"Ctrl+K", "Kanal / sunucu / komut ara"},
      {"Tab / Shift+Tab", "Paneller arasında odak değiştir"},
      {"Oklar + Enter", "Sunucu veya kanal seç; ses kanalına katıl"},
      {"Alt+1…9", "Sunucuya hızlı geç"},
      {"Ctrl+1 / 2 / 3 / 4", "Sohbet / ses / arkadaşlar / olaylar"},
      {"Enter / Alt+Enter", "Mesaj gönder / yeni satır"},
      {"Ctrl+R / Ctrl+E", "Mesaja yanıt / kendi mesajını düzenle"},
      {"Ctrl+Delete", "Kendi mesajını onaylayarak sil"},
      {"Ctrl+G / Ctrl+P", "Emoji tepkisi / seçili mesajın profili"},
      {"PgUp / PgDn", "Geçmişi yükle / mesajlarda kaydır"},
      {"Alt+M / D / L", "Gönderilen ses / sağır / yerel hoparlör"},
      {"Alt+V / Alt+A", "Video / ses dosyası; formda F2 ile seç"},
      {"Alt+R / S / O", "Kayıt başlat / bitir / klasörü aç"},
      {"Alt+C / Alt+G", "Kamera / Go Live çıkışı (bağlantı korunur)"},
      {"F3 / F5 / F6", "Yerel arama / yenile / arkadaşlar"},
      {"Ctrl+Q", "Çık; taslakları bu bilgisayarda koru"}};
  int y = a.y + 4;
  for (const auto &[key, action] : help) {
    if (y >= a.y + a.h - 1)
      break;
    c.put(a.x + 2, y, fit(key, std::min(23, a.w / 3)), Accent, Base,
          std::min(23, a.w / 3));
    c.put(a.x + 3 + std::min(23, a.w / 3), y, action, Text, Base,
          a.w - 6 - std::min(23, a.w / 3));
    ++y;
  }
}
void App::draw_modal(Canvas &c) {
  if (help_open_) {
    Rect box{std::max(1, (c.width - 92) / 2), 2, std::min(c.width - 2, 92),
             std::min(c.height - 4, 25)};
    c.box(box, "YARDIM · Esc kapatır", Accent, Raised);
    draw_help(c, {box.x + 1, box.y + 2, box.w - 2, box.h - 3});
    return;
  }
  if (prompt_kind_.empty())
    return;
  int w = std::min(c.width - 4, 82),
      h = prompt_kind_ == "palette"   ? std::min(19, c.height - 5)
          : prompt_kind_ == "profile" ? std::min(20, c.height - 5)
                                      : 9;
  Rect a{(c.width - w) / 2, std::max(1, (c.height - h) / 3), w, h};
  c.box(a, prompt_title_, Accent, Raised);
  if (prompt_kind_ == "profile") {
    auto u = profile_.value("user", Json::object());
    c.put(a.x + 3, a.y + 2, str(u, "global_name", str(u, "username")), White,
          Raised, a.w - 6, true);
    c.put(a.x + 3, a.y + 3, "@" + str(u, "username") + " · " + str(u, "id"),
          Dim, Raised, a.w - 6);
    c.put(a.x + 3, a.y + 5, str(profile_, "pronouns"), Violet, Raised, a.w - 6);
    int y = a.y + 7;
    for (const auto &line :
         wrap(str(profile_, "bio", "Biyografi bu hesaba görünmüyor."),
              a.w - 6)) {
      if (y >= a.y + a.h - 3)
        break;
      c.put(a.x + 3, y++, line, Text, Raised, a.w - 6);
    }
    c.put(a.x + 3, a.y + a.h - 2, "Enter DM aç · Esc kapat", Accent, Raised,
          a.w - 6);
    return;
  }
  if (prompt_kind_ == "delete") {
    auto m = selected_message();
    c.put(a.x + 3, a.y + 2, "Bu işlem Discord'daki kendi mesajını siler.", Red,
          Raised, a.w - 6);
    c.put(a.x + 3, a.y + 4, fit(str(m, "content"), a.w - 6, true), Text, Raised,
          a.w - 6);
    c.put(a.x + 3, a.y + a.h - 2, "Enter: sil · Esc: vazgeç", Red, Raised,
          a.w - 6);
    return;
  }
  c.fill({a.x + 2, a.y + 2, a.w - 4, 2}, Composer);
  std::string visible = prompt_.text;
  while (columns(visible) > a.w - 8)
    visible.erase(0, next(visible, 0));
  c.put(a.x + 3, a.y + 2,
        visible.empty() && prompt_kind_ == "palette" ? "Nereye gitmek istersin?"
                                                     : visible,
        visible.empty() ? Dim : White, Composer, a.w - 6);
  c.put(a.x + 3 + std::min(columns(visible), a.w - 7), a.y + 2, "▏", Mint,
        Composer);
  if (prompt_kind_ == "palette") {
    auto list = palette();
    int room = a.h - 7;
    if (!list.empty())
      modal_index_ = std::clamp(modal_index_, 0, int(list.size() - 1));
    int start = std::max(0, modal_index_ - room + 1);
    if (list.empty())
      c.put(a.x + 3, a.y + 5, "Eşleşme yok.", Dim, Raised, a.w - 6);
    for (int i = 0; i < room && start + i < int(list.size()); ++i) {
      auto &entry = list[size_t(start + i)];
      auto bg = start + i == modal_index_ ? Selected : Raised;
      int y = a.y + 5 + i;
      c.fill({a.x + 2, y, a.w - 4, 1}, bg);
      c.put(a.x + 3, y, str(entry, "name"),
            start + i == modal_index_ ? White : Text, bg, a.w - 21);
      c.put(a.x + a.w - 17, y, str(entry, "detail"), Dim, bg, 14);
      hits_.push_back({{a.x + 2, y, a.w - 4, 1}, "palette", start + i});
    }
    c.put(a.x + 3, a.y + a.h - 2, "↑↓ seç · Enter aç · Esc kapat", Dim, Raised,
          a.w - 6);
  } else
    c.put(a.x + 3, a.y + a.h - 2,
          error_ ? status_ : "Enter uygula · Esc iptal · Ctrl+U temizle",
          error_ ? Red : Dim, Raised, a.w - 6);
}
std::string App::render(int width, int height) {
  width = std::max(40, width);
  height = std::max(16, height);
  Canvas c(width, height);
  hits_.clear();
  int rail = width >= 75 ? 7 : 0,
      left = width >= 120  ? 27
             : width >= 85 ? 23
                           : 18,
      right = width >= 132 ? 28 : 0;
  int main_x = rail + left + 1,
      main_w = width - main_x - (right ? right + 1 : 0);
  int top = 3, bottom = height - 5;
  c.fill({0, 0, width, 2}, Rail);
  c.put(2, 0, "N O V A", Mint, Rail, 10, true);
  c.put(14, 0,
        demo_   ? "DEMO · ağ bağlantısı yok"
        : live_ ? "Discord C++  ·  canlı"
                : "Discord C++  ·  bağlanıyor",
        live_ ? Mint : Dim, Rail, width - 40);
  c.put(std::max(25, width - 25), 0, "Ctrl+K ara   F1 yardım", Dim, Rail, 24);
  hits_.push_back({{width - 10, 0, 10, 2}, "help"});
  c.put(2, 1, "Terminalde daha sakin bir iletişim alanı.", Dim, Rail,
        width - 29);
  c.put(std::max(1, width - 18), 1, "Ctrl+Q çıkış", Dim, Rail, 16);
  c.fill({0, 2, rail, height - 7}, Rail);
  c.fill({rail, 2, left, height - 7}, Sidebar);
  c.vertical(rail + left, 2, height - 7);
  if (rail) {
    int room = std::max(1, (bottom - 4) / 3),
        start = std::max(0, guild_index_ - room + 1);
    for (int row = 0; row < room && start + row <= int(guilds_.size()); ++row) {
      int index = start + row, y = top + row * 3;
      auto name = index ? str(guilds_[size_t(index - 1)], "name") : "Ana sayfa";
      auto badge = index ? fit(name, 2) : "⌂";
      bool selected = index == guild_index_;
      auto bg = selected ? Selected : Rail;
      c.fill({1, y, 5, 2}, bg);
      c.put(2, y, badge, selected ? Mint : Dim, bg, 3, true);
      if (selected)
        c.put(0, y, "▌", Mint, Rail);
      hits_.push_back({{0, y, rail, 2}, "guild", index});
    }
  }
  c.put(rail + 2, top, fit(guild_name(), left - 4, true), White, Sidebar,
        left - 4, true);
  c.put(rail + 2, top + 1, guild_.empty() ? "DOĞRUDAN MESAJLAR" : "KANALLAR",
        Dim, Sidebar, left - 4);
  c.line(rail + 1, top + 2, left - 2, Dim, Sidebar);
  auto nav = navigation();
  int room = std::max(1, bottom - top - 4);
  channel_index_ =
      std::clamp(channel_index_, 0, std::max(0, int(nav.size()) - 1));
  int nav_start = std::max(0, channel_index_ - room + 1);
  if (nav.empty())
    c.put(rail + 2, top + 4,
          guild_.empty() ? "DM listesi boş" : "Kanallar yükleniyor…", Dim,
          Sidebar, left - 4);
  for (int i = 0; i < room && nav_start + i < int(nav.size()); ++i) {
    int index = nav_start + i, y = top + 4 + i;
    auto &item = nav[size_t(index)];
    int type = item.value("type", 0);
    bool selected = focus_ == 1 && index == channel_index_,
         current = str(item, "id") == text_channel_,
         connected =
             media_active_ && str(item, "id") == str(voice_, "channel_id");
    auto bg = selected || current ? Selected : Sidebar;
    c.fill({rail + 1, y, left - 2, 1}, bg);
    std::string prefix = type == 4                ? "⌄ "
                         : voice_type(type)       ? "◉ "
                         : type == 1 || type == 3 ? "@ "
                                                  : "# ";
    auto unread = unread_[str(item, "id")];
    std::string name = prefix + str(item, "name");
    if (unread)
      name += " (" + std::to_string(unread) + ")";
    c.put(rail + 2, y, fit(name, left - 4, true),
          connected             ? Mint
          : type == 4           ? Dim
          : selected || current ? White
                                : Text,
          bg, left - 4, selected || type == 4);
    hits_.push_back({{rail + 1, y, left - 2, 1}, "channel", index});
  }
  const std::vector<std::pair<std::string, std::string>> views = {
      {"Sohbet", "chat"},
      {"Ses", "voice"},
      {"Arkadaşlar", "friends"},
      {"Olaylar", "events"}};
  int tab_x = main_x + 1;
  for (size_t i = 0; i < views.size(); ++i) {
    bool selected = size_t(view_) == i;
    auto label = " " + views[i].first + " ";
    int w = columns(label);
    if (tab_x + w >= main_x + main_w)
      break;
    c.put(tab_x, 2, label, selected ? Mint : Dim, selected ? Raised : Base, w,
          selected);
    hits_.push_back({{tab_x, 2, w, 1}, views[i].second});
    tab_x += w + 1;
  }
  Rect area{main_x, 4, main_w, bottom - 4};
  if (view_ == View::Chat)
    draw_chat(c, area);
  else if (view_ == View::Voice)
    draw_voice(c, area);
  else if (view_ == View::Friends)
    draw_friends(c, area);
  else if (view_ == View::Help)
    draw_help(c, area);
  else {
    c.put(area.x + 2, area.y, "OTURUM OLAYLARI", White, Base, area.w - 4, true);
    c.put(area.x + 2, area.y + 1,
          "Yalnızca bu oturumdaki işlemler. Sohbet içeriği günlüğe yazılmaz.",
          Dim, Base, area.w - 4);
    int y = area.y + 3;
    int index = 0;
    for (auto &[time, text] : events_) {
      if (index++ < scroll_)
        continue;
      for (auto &line : wrap(time + "  " + text, area.w - 4, true)) {
        if (y >= area.y + area.h)
          break;
        c.put(area.x + 2, y++, line, Text, Base, area.w - 4);
      }
      if (y >= area.y + area.h)
        break;
    }
  }
  if (right) {
    c.vertical(main_x + main_w, 2, height - 7);
    draw_sidebar(c, {main_x + main_w + 1, 2, right, height - 7});
  }
  c.fill({0, bottom, width, 4}, Raised);
  c.line(0, bottom, width, Dim, Raised);
  auto self =
      str(me_, "global_name",
          str(me_, "username", demo_ ? "Demo hesabı" : "Hesap yükleniyor"));
  c.put(2, bottom + 1, fit(self, std::min(24, width / 4), true), White, Raised,
        std::min(24, width / 4), true);
  std::string vc = media_active_ ? "● Ses bağlı" : "○ Ses bağlantısı yok";
  c.put(std::max(20, width / 4 + 2), bottom + 1, vc, media_active_ ? Mint : Dim,
        Raised, width / 2);
  auto control = [&](int x, std::string label, std::string action, Tone tone) {
    int w = columns(label) + 2;
    c.put(x, bottom + 2, " " + label + " ", tone, Raised, w);
    hits_.push_back({{x, bottom + 1, w, 2}, std::move(action)});
    return x + w + 1;
  };
  int x = 1;
  x = control(x,
              voice_.value("self_mute", config_.value("muted", false))
                  ? "Mikrofon ×"
                  : "Mikrofon ●",
              "mute", voice_.value("self_mute", false) ? Red : Mint);
  x = control(x,
              voice_.value("self_deaf", false) ? "Gelen ses ×" : "Gelen ses ●",
              "deaf", voice_.value("self_deaf", false) ? Red : Text);
  x = control(x,
              voice_.value("audio_playback_active", false) ? "Yerel ses ●"
                                                           : "Yerel ses ×",
              "playback",
              voice_.value("audio_playback_active", false) ? Amber : Dim);
  if (x + 16 < width)
    x = control(x, voice_.value("recording", false) ? "KAYIT ●" : "Kayıt",
                "record", voice_.value("recording", false) ? Red : Text);
  if (x + 11 < width)
    control(x, "Ayrıl", "leave", Red);
  c.put(2, bottom + 3, fit(status_, width - 4, true), error_ ? Red : Amber,
        Raised, width - 4);
  c.fill({0, height - 1, width, 1}, Rail);
  c.put(2, height - 1,
        focus_ == 0   ? "SUNUCULAR"
        : focus_ == 1 ? "KANALLAR"
        : focus_ == 3 ? "KATILIMCILAR"
        : composing_  ? "YAZIYOR"
                      : "SOHBET",
        Mint, Rail, 14, true);
  c.put(18, height - 1,
        "Tab odak  ·  Ctrl+K hızlı geçiş  ·  F4 medya  ·  F1 kısayollar", Dim,
        Rail, width - 20);
  draw_modal(c);
  return c.render();
}
void App::seed_demo() {
  demo_ = true;
  live_ = true;
  initial_ = false;
  me_ = {{"id", "100"}, {"username", "cavan"}, {"global_name", "Cavan"}};
  guilds_ = Json::array({{{"id", "400"}, {"name", "Gece Vardiyası"}},
                         {{"id", "401"}, {"name", "Atölye"}},
                         {{"id", "402"}, {"name", "Arkadaşlar"}},
                         {{"id", "403"}, {"name", "Müzik Odası"}}});
  channels_["400"] = Json::array(
      {{{"id", "500"}, {"name", "SOHBET"}, {"type", 4}, {"position", 0}},
       {{"id", "200"},
        {"name", "genel"},
        {"type", 0},
        {"parent_id", "500"},
        {"position", 1},
        {"guild_id", "400"},
        {"topic",
         "Bir fikir bırak, bir sohbete katıl. Burası bizim küçük alanımız."}},
       {{"id", "201"},
        {"name", "paylaşımlar"},
        {"type", 0},
        {"parent_id", "500"},
        {"position", 2},
        {"guild_id", "400"}},
       {{"id", "501"}, {"name", "BİRLİKTE"}, {"type", 4}, {"position", 3}},
       {{"id", "202"},
        {"name", "Gece odası"},
        {"type", 2},
        {"parent_id", "501"},
        {"position", 4},
        {"guild_id", "400"}},
       {{"id", "203"},
        {"name", "Müzik köşesi"},
        {"type", 2},
        {"parent_id", "501"},
        {"position", 5},
        {"guild_id", "400"}}});
  dms_ = Json::array({{{"id", "300"}, {"name", "Deniz"}, {"type", 1}},
                      {{"id", "301"}, {"name", "Mira"}, {"type", 1}}});
  friends_ = Json::array(
      {{{"id", "101"},
        {"type", 1},
        {"user",
         {{"id", "101"}, {"username", "deniz"}, {"global_name", "Deniz"}}}},
       {{"id", "102"},
        {"type", 1},
        {"user",
         {{"id", "102"}, {"username", "mira"}, {"global_name", "Mira"}}}},
       {{"id", "103"},
        {"type", 3},
        {"user",
         {{"id", "103"}, {"username", "arda"}, {"global_name", "Arda"}}}}});
  presence_["101"] = "online";
  presence_["102"] = "idle";
  guild_ = "400";
  guild_index_ = 1;
  text_channel_ = "200";
  channel_index_ = 1;
  focus_ = 2;
  auto add = [&](std::string id, std::string author, std::string name,
                 std::string text, std::string time) {
    merge({{"id", id},
           {"channel_id", "200"},
           {"author",
            {{"id", author}, {"username", name}, {"global_name", name}}},
           {"content", text},
           {"timestamp", "2026-10-29T" + time + ":00Z"}});
  };
  add("900", "101", "Deniz", "Yeni terminal arayüzü nasıl görünüyor?", "12:30");
  add("901", "100", "Cavan",
      "Daha az kalabalık, ama her şey bir arada. Sohbet soldan seçiliyor; ses "
      "sağda hep görünür.",
      "12:31");
  add("902", "102", "Mira",
      "Ctrl+K ile kanallar arasında geçmek çok rahat. Türkçe karakterler ve "
      "uzun mesajlar da düzgün akıyor.",
      "12:32");
  add("903", "101", "Deniz",
      "Bu akşam Gece odasında buluşalım. Yayın açarsan buradan izlemeye "
      "katılabiliyoruz.",
      "12:33");
  add("904", "100", "Cavan",
      "Yerel ses kapalı kalıyor. Kayıt, mikrofon ve gelen ses artık "
      "birbirinden bağımsız.",
      "12:34");
  add("905", "102", "Mira",
      "Tamamdır. Bir de mesajı seçip Ctrl+R ile yanıtla ✨", "12:35");
  voice_ = {
      {"channel_id", "202"},
      {"guild_id", "400"},
      {"self_deaf", false},
      {"self_mute", false},
      {"audio_playback_active", false},
      {"participants",
       Json::array(
           {{{"user_id", "100"}, {"username", "Cavan"}, {"camera", true}},
            {{"user_id", "101"}, {"username", "Deniz"}, {"streaming", true}},
            {{"user_id", "102"}, {"username", "Mira"}, {"self_mute", true}}})},
      {"streams", Json::array({{{"owner_id", "101"},
                                {"key", "guild:400:202:101"},
                                {"watching", true},
                                {"viewer_count", 2}}})},
      {"transports",
       {{"voice",
         {{"connected", true},
          {"encrypted", true},
          {"video_interval_mean_ms", 33.33},
          {"video_frames_sent", 1234},
          {"received_frames", 932}}}}}};
  media_active_ = true;
  config_["video_source"] = "C:/media/Gece manzarası.mp4";
  config_["audio_source"] = "C:/media/sakin-müzik.mp3";
  note("Demo görünümü · Bu ekranda gerçek Discord işlemi yapılmaz.");
}
void App::self_test() {
  auto require = [](bool value, const char *why) {
    if (!value)
      throw std::runtime_error(why);
  };
  require(columns("A界B") == 4, "Wide character width");
  require(columns("ş") == 1, "Combining character width");
  require(wrap("Türkçe uzun mesaj", 10, true) ==
              std::vector<std::string>{"Türkçe", "uzun mesaj"},
          "Word wrapping split a Turkish word");
  require(wrap("abcdefghijk", 5, true) ==
              std::vector<std::string>{"abcde", "fghij", "k"},
          "Long unbroken text overflowed");
  require(clean("x\x1b[31my").find('\x1b') == std::string::npos,
          "Terminal escape injection");
  Editor edit;
  edit.set("Türkçe 世界");
  edit.left();
  edit.erase();
  require(clean(edit.text) == edit.text, "UTF-8 edit corruption");
  edit.set("old", true);
  edit.insert("yeni");
  require(edit.text == "yeni", "Selected input replacement");
  App app(nullptr, Json::object(), {}, true);
  app.seed_demo();
  auto before = app.summary().at("messages").get<size_t>();
  app.input({"ENTER"});
  app.input({"", "test mesajı"});
  require(app.summary()["messages"] == before,
          "Typing sent a message without Enter");
  app.input({"ENTER"});
  require(app.summary()["messages"] == before + 1, "Explicit send failed");
  app.input({"ESC"});
  app.input({"UP"});
  auto selected = app.selected_message();
  require(str(selected.value("author", Json::object()), "id") != "100",
          "Expected foreign message selection");
  app.command("delete");
  require(app.prompt_kind_.empty(), "Foreign message deletion was offered");
  app.input({"CTRL_K"});
  app.input({"", "yerel ses"});
  require(!app.palette().empty(), "Command palette search failed");
  app.input({"ESC"});
  App protected_app(nullptr, Json::object(), {}, true);
  App media_app(nullptr, Json::object(), {}, true);
  media_app.seed_demo();
  media_app.voice_["recording"] = true;
  media_app.command("camera");
  media_app.command("stream");
  require(!media_app.config_.value("camera", true) &&
              !media_app.config_.value("go_live", true) &&
              media_app.media_active_ &&
              media_app.voice_.value("recording", false),
          "Output toggles left voice or interrupted recording");
  protected_app.seed_demo();
  protected_app.input({"ENTER"});
  protected_app.input({"", "korunacak taslak"});
  protected_app.apply({{"type", "error"},
                       {"writes_blocked", true},
                       {"message", "fixture restriction"}});
  auto protected_count = protected_app.summary()["messages"];
  protected_app.input({"ENTER"});
  require(protected_app.summary()["messages"] == protected_count &&
              protected_app.drafts_[protected_app.text_channel_].edit.text ==
                  "korunacak taslak",
          "Restricted account sent a message or lost its draft");
  App cached(nullptr, Json::object(), {}, true);
  cached.seed_demo();
  cached.render(150, 42);
  auto layout = cached.chat_lines_.size();
  cached.input({"", "draft only"});
  cached.render(150, 42);
  require(cached.chat_lines_.size() == layout &&
              cached.layout_revision_ == cached.message_revision_,
          "Composer changed the history layout");
  cached.apply({{"type", "message_edit"},
                {"channel", "200"},
                {"id", "900"},
                {"patch", {{"content", "layout invalidation fixture"}}}});
  cached.render(150, 42);
  require(std::any_of(cached.chat_lines_.begin(), cached.chat_lines_.end(),
                      [](const auto &line) {
                        return line.text == "layout invalidation fixture";
                      }),
          "Edited message kept a stale history layout");
  cached.apply({{"type", "error"},
                {"operation", "history:200"},
                {"status", 403},
                {"message", "Missing Access"}});
  require(cached.history_errors_.contains("200") && !cached.writes_blocked_,
          "Channel permission failure confused with account restriction");
  cached.apply(
      {{"type", "history"}, {"channel", "200"}, {"items", Json::array()}});
  require(cached.history_errors_.empty(),
          "Successful history did not clear its error");
  for (int i = 0; i < 100; ++i) {
    auto channel = std::to_string(9000 + i);
    cached.apply(
        {{"type", "message_delete"}, {"channel", channel}, {"id", "1"}});
    cached.apply({{"type", "message"},
                  {"data", {{"channel_id", channel}, {"id", "2"}}}});
  }
  require(cached.deleted_.empty() && cached.messages_.size() == 1,
          "Offscreen events allocated unused history or tombstones");
  App paging(nullptr, Json::object(), {}, true);
  paging.text_channel_ = "200";
  for (int i = 1000; i < 1510; ++i)
    paging.merge({{"id", std::to_string(i)}, {"channel_id", "200"}});
  require(paging.messages_.at("200").size() == 500 &&
              str(paging.messages_.at("200").front(), "id") == "1010",
          "Recent history limit or snowflake order failed");
  Json older = Json::array();
  for (int i = 1009; i >= 960; --i)
    older.push_back({{"id", std::to_string(i)}, {"channel_id", "200"}});
  paging.apply({{"type", "history"},
                {"channel", "200"},
                {"older", true},
                {"items", older}});
  require(paging.messages_.at("200").size() == 500 &&
              str(paging.messages_.at("200").front(), "id") == "960" &&
              str(paging.messages_.at("200").back(), "id") == "1459",
          "Older page was discarded at the history limit");
  paging.merge({{"id", "960"}, {"channel_id", "200"}, {"content", "updated"}});
  require(paging.messages_.at("200").size() == 500 &&
              str(paging.messages_.at("200").front(), "content") == "updated",
          "Sorted insertion duplicated a known message");
  for (int i = 1; i <= 11; ++i)
    paging.merge({{"id", "1"}, {"channel_id", std::to_string(i)}});
  paging.choose_channel({{"id", "1"}, {"type", 0}});
  paging.merge({{"id", "1"}, {"channel_id", "12"}});
  require(paging.messages_.size() == 12 && paging.history_use_.size() == 12 &&
              paging.messages_.contains("1") &&
              !paging.messages_.contains("200"),
          "History eviction did not retain the recently visited channel");
  for (auto [w, h] : {std::pair{150, 42}, std::pair{100, 30}, std::pair{80, 25},
                      std::pair{60, 20}})
    for (int view = 0; view < 4; ++view) {
      app.set_view(view);
      auto frame = app.render(w, h);
      require(!frame.empty(), "Empty responsive frame");
    }
}
} // namespace nova
