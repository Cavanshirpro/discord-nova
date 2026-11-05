#include "backend.hpp"
#include "media_config.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <set>
#include <thread>
namespace nova {
std::string str(const Json &j, const char *key, std::string fallback) {
  auto p = j.find(key);
  if (p == j.end() || p->is_null())
    return fallback;
  if (p->is_string())
    return p->get<std::string>();
  if (p->is_number_integer())
    return p->dump();
  return fallback;
}
Json user_json(const discord::User &u) {
  return {{"id", u.id.str()},
          {"username", u.name},
          {"global_name", u.display_name},
          {"bot", u.bot}};
}
Json message_json(const discord::Message &m) {
  Json attachments = Json::array();
  for (const auto &a : m.attachments)
    attachments.push_back(
        {{"filename", a.filename}, {"size", a.size}, {"url", a.url}});
  return {{"id", m.id.str()},
          {"channel_id", m.channel_id.str()},
          {"author", user_json(m.author)},
          {"content", m.content},
          {"timestamp", m.timestamp},
          {"edited", bool(m.edited_timestamp)},
          {"attachments", attachments}};
}
static Json channel_json(const discord::Channel &c) {
  std::string name = c.name;
  if (name.empty())
    for (const auto &u : c.recipients) {
      if (!name.empty())
        name += " , ";
      name += u.display_name;
    }
  return {{"id", c.id.str()},
          {"name", name.empty() ? c.id.str() : name},
          {"type", c.type},
          {"position", c.position},
          {"parent_id", c.parent_id ? c.parent_id->str() : ""},
          {"guild_id", c.guild_id ? c.guild_id->str() : ""},
          {"topic", c.topic.value_or("")}};
}
struct Backend::Impl {
  discord::Client api, live;
  std::string token, me;
  discord::MediaOptions media_options;
  std::filesystem::path recordings;
  std::mutex mutex, media_mutex;
  std::condition_variable changed;
  struct Job {
    std::string key;
    std::function<void()> run;
    uint64_t request = 0;
  };
  std::deque<Job> jobs;
  std::set<std::string> pending;
  std::deque<Json> events;
  std::jthread worker, gateway_thread, media_thread;
  std::shared_ptr<discord::MediaSession> media_session;
  std::atomic_bool stopping = false, media_alive = false;
  std::atomic_bool gateway_running = false;
  std::atomic_bool writes_blocked = false;
  bool started = false;
  Impl(Json config, std::string value, std::filesystem::path root,
       const discord::ClientOptions &options)
      : api(options), live(options), token(std::move(value)),
        recordings(std::move(root)) {
    media_options.guild_id = config.at("guild_id").get<discord::Snowflake>();
    media_options.channel_id =
        config.at("channel_id").get<discord::Snowflake>();
    media_options.ffmpeg = str(config, "ffmpeg", "ffmpeg");
    media_options.video_source = str(config, "video_source");
    media_options.audio_source = str(config, "audio_source");
    media_options.camera = config.value("camera", true);
    media_options.go_live = config.value("go_live", true);
    media_options.loop = config.value("loop", true);
    media_options.width = config.value("width", 1280);
    media_options.height = config.value("height", 720);
    media_options.fps = config.value("fps", 30);
    media_options.bitrate_kbps = config.value("bitrate_kbps", 1800);
    media_options.watch_streams = config.value("watch_streams", false);
    media_options.receive_audio = config.value("receive_audio", true);
    media_options.receive_video = config.value("receive_video", true);
    media_options.play_received_audio =
        config.value("play_received_audio", false);
    media_options.muted = config.value("muted", false);
    media_options.voice_audio = config.value("voice_audio", true);
    media_options.stream_audio = config.value("stream_audio", false);
    media_options.cache_directory =
        media_example::path(str(config, "cache_directory"));
  }
  void post(Json value) {
    std::lock_guard lock(mutex);
    if (events.size() >= 2048) {
      auto old = std::find_if(events.begin(), events.end(), [](const Json &e) {
        return str(e, "type") == "media" &&
               str(e["data"], "event") == "snapshot";
      });
      if (old != events.end())
        events.erase(old);
      else
        events.pop_front();
    }
    events.push_back(std::move(value));
  }
  void error(const std::exception &e, std::string operation,
             uint64_t request = 0) {
    int status = 0, code = 0;
    const bool verification =
        dynamic_cast<const discord::VerificationRequired *>(&e) != nullptr;
    if (auto http = dynamic_cast<const discord::HttpError *>(&e)) {
      status = http->status;
      code = http->code;
    }
    const auto gateway = dynamic_cast<const discord::GatewayError *>(&e);
    if (verification || status == 401 || code == 40002 || code == 40003 ||
        code == 40004 || code == 20026 || (gateway && gateway->code == 4004))
      writes_blocked = true;
    post({{"type", "error"},
          {"operation", operation},
          {"request", request},
          {"status", status},
          {"code", code},
          {"gateway_code", gateway ? gateway->code : 0},
          {"verification_required", verification},
          {"writes_blocked", writes_blocked.load()},
          {"message",
           verification
               ? "Discord hesap doğrulaması istiyor; işlem tekrarlanmadı."
               : e.what()}});
  }
  void enqueue(std::string key, std::function<void()> task,
               uint64_t request = 0) {
    bool full = false;
    {
      std::lock_guard lock(mutex);
      if (stopping)
        return;
      if (pending.contains(key) && !key.starts_with("media:"))
        return;
      if (jobs.size() >= 64)
        full = true;
      else {
        pending.insert(key);
        jobs.push_back({std::move(key), std::move(task), request});
      }
    }
    if (full)
      post({{"type", "error"},
            {"request", request},
            {"message", "İşlem kuyruğu dolu; biraz sonra tekrar dene."}});
    changed.notify_one();
  }
  void require_write() const {
    if (writes_blocked)
      throw discord::Error(
          "Hesap koruması nedeniyle yazma işlemleri durduruldu. Discord "
          "uygulamasında hesabını kontrol et; taslağın korundu.");
  }
  void bootstrap() {
    enqueue("me", [this] {
      auto user = api.fetch_me();
      me = user.id.str();
      post({{"type", "me"}, {"user", user_json(user)}});
    });
    enqueue("guilds", [this] {
      Json list = Json::array();
      for (auto &g : api.fetch_guilds())
        list.push_back({{"id", g.id.str()},
                        {"name", g.name},
                        {"members", g.approximate_member_count.value_or(0)}});
      post({{"type", "guilds"}, {"items", list}});
    });
    enqueue("dms", [this] {
      Json list = Json::array();
      auto data = api.request("GET", "/users/@me/channels");
      for (const auto &c : data) {
        auto channel = c.get<discord::Channel>();
        list.push_back(channel_json(channel));
      }
      post({{"type", "dms"}, {"items", list}});
    });
    enqueue("friends", [this] {
      Json list = Json::array();
      for (auto &r : api.fetch_relationships())
        list.push_back({{"id", r.id.str()},
                        {"type", int(r.type)},
                        {"user", r.user ? user_json(*r.user)
                                        : Json{{"id", r.id.str()},
                                               {"username", r.id.str()}}}});
      post({{"type", "friends"}, {"items", list}});
    });
  }
  std::shared_ptr<discord::MediaSession> current() {
    std::lock_guard lock(media_mutex);
    if (!media_session || !media_alive || !media_session->is_running())
      throw discord::Error("Önce bir ses kanalına katıl.");
    return media_session;
  }
  void start_media(std::string guild, std::string channel) {
    if (!discord::MediaSession::available())
      throw discord::Error("Bu derlemede ses/video kapalı. NOVA_NATIVE_MEDIA=ON ile derle.");
    std::shared_ptr<discord::MediaSession> old;
    {
      std::lock_guard lock(media_mutex);
      old = media_session;
    }
    if (old && media_alive) {
      const auto state = old->snapshot();
      if (str(state, "guild_id") == guild &&
          str(state, "channel_id") == channel)
        return;
      old->join(discord::Snowflake(guild), discord::Snowflake(channel));
      return;
    }
    if (old)
      old->stop();
    if (media_thread.joinable())
      media_thread.join();
    discord::MediaOptions options;
    {
      std::lock_guard lock(media_mutex);
      media_options.guild_id = discord::Snowflake(guild);
      media_options.channel_id = discord::Snowflake(channel);
      options = media_options;
    }
    if (options.video_source.empty())
      options.camera = options.go_live = false;
    auto session = std::make_shared<discord::MediaSession>(options);
    {
      std::lock_guard lock(media_mutex);
      media_session = session;
    }
    media_alive = true;
    media_thread = std::jthread([this, session] {
      try {
        session->run(token, [this](const Json &event) {
          if (str(event, "event") == "snapshot") {
            std::lock_guard lock(media_mutex);
            media_options.guild_id =
                event.at("guild_id").get<discord::Snowflake>();
            media_options.channel_id =
                event.at("channel_id").get<discord::Snowflake>();
            media_options.video_source = str(event, "video_source");
            media_options.audio_source = str(event, "audio_source");
            media_options.muted = event.value("self_mute", false);
            media_options.play_received_audio =
                event.value("play_received_audio", false);
          }
          post({{"type", "media"}, {"data", event}});
        });
      } catch (const std::exception &e) {
        error(e, "media");
      }
      media_alive = false;
      post({{"type", "media_stopped"}});
    });
  }
};
Backend::Backend(Json config, std::string token,
                 std::filesystem::path recordings,
                 discord::ClientOptions options)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(token),
                                   std::move(recordings), options)) {}
Backend::~Backend() { stop(); }
void Backend::start(bool gateway) {
  auto &p = *impl_;
  if (p.started)
    return;
  p.started = true;
  p.api.login(p.token);
  p.worker = std::jthread([&p] {
    while (!p.stopping) {
      Impl::Job job;
      {
        std::unique_lock lock(p.mutex);
        p.changed.wait(lock, [&] { return p.stopping || !p.jobs.empty(); });
        if (p.stopping)
          break;
        job = std::move(p.jobs.front());
        p.jobs.pop_front();
      }
      try {
        job.run();
      } catch (const std::exception &e) {
        p.error(e, job.key, job.request);
      }
      {
        std::lock_guard lock(p.mutex);
        p.pending.erase(job.key);
      }
    }
  });
  p.bootstrap();
  if (!gateway)
    return;
  p.live.on_ready([&p](const discord::Ready &r) {
    p.post({{"type", "connection"}, {"online", true}});
    p.post({{"type", "me"}, {"user", user_json(r.user)}});
  });
  p.live.on_resumed(
      [&p] { p.post({{"type", "connection"}, {"online", true}}); });
  p.live.on_message([&p](const discord::Message &m) {
    p.post({{"type", "message"}, {"data", message_json(m)}});
  });
  p.live.on_message_edit([&p](const discord::MessageUpdate &e) {
    p.post({{"type", "message_edit"},
            {"channel", e.channel_id.str()},
            {"id", e.id.str()},
            {"patch", e.patch}});
  });
  p.live.on_message_delete([&p](const discord::MessageDelete &e) {
    p.post({{"type", "message_delete"},
            {"channel", e.channel_id.str()},
            {"id", e.id.str()}});
  });
  p.live.on_raw_event([&p](const discord::RawEvent &e) {
    if (e.name == "TYPING_START")
      p.post({{"type", "typing"}, {"data", e.data}});
    else if (e.name == "PRESENCE_UPDATE")
      p.post({{"type", "presence"}, {"data", e.data}});
    else if (e.name.starts_with("RELATIONSHIP_"))
      p.bootstrap();
  });
  p.live.on_error(
      [&p](const std::exception &e) { p.error(e, "gateway_callback"); });
  p.gateway_running = true;
  p.gateway_thread = std::jthread([&p] {
    try {
      p.live.run(p.token);
    } catch (const std::exception &e) {
      if (!p.stopping)
        p.error(e, "gateway");
    }
    p.post({{"type", "connection"}, {"online", false}});
    p.gateway_running = false;
  });
}
void Backend::stop() {
  if (!impl_ || impl_->stopping.exchange(true))
    return;
  auto &p = *impl_;
  p.live.close();
  p.api.close();
  {
    std::lock_guard lock(p.media_mutex);
    if (p.media_session)
      p.media_session->stop();
  }
  p.changed.notify_all();
  if (p.worker.joinable())
    p.worker.join();
  {
    std::lock_guard lock(p.media_mutex);
    if (p.media_session)
      p.media_session->stop();
  }
  if (p.media_thread.joinable())
    p.media_thread.join();
  if (p.gateway_thread.joinable()) {
    // Client::run resets its stop flag on startup. Repeat close until the
    // worker exits so an immediate quit cannot lose the cancellation.
    while (p.gateway_running) {
      p.live.close();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    p.gateway_thread.join();
  }
  std::fill(p.token.begin(), p.token.end(), '\0');
}
std::vector<Json> Backend::drain() {
  std::lock_guard lock(impl_->mutex);
  std::vector<Json> out;
  while (!impl_->events.empty()) {
    out.push_back(std::move(impl_->events.front()));
    impl_->events.pop_front();
  }
  return out;
}
void Backend::refresh() { impl_->bootstrap(); }
void Backend::channels(std::string guild) {
  auto *p = impl_.get();
  p->enqueue("channels:" + guild, [p, guild] {
    Json list = Json::array();
    for (auto &c : p->api.fetch_channels(discord::Snowflake(guild))) {
      auto row = channel_json(c);
      row["guild_id"] = guild;
      list.push_back(row);
    }
    p->post({{"type", "channels"}, {"guild", guild}, {"items", list}});
  });
}
void Backend::history(std::string channel, std::string before) {
  auto *p = impl_.get();
  p->enqueue("history:" + channel, [p, channel, before] {
    auto path =
        "/channels/" + discord::Snowflake(channel).str() + "/messages?limit=50";
    if (!before.empty())
      path += "&before=" + discord::Snowflake(before).str();
    Json list = Json::array();
    for (const auto &raw : p->api.request("GET", path)) {
      auto row = message_json(raw.get<discord::Message>());
      if (raw.contains("message_reference"))
        row["reply_to"] = str(raw["message_reference"], "message_id");
      if (raw.contains("referenced_message") &&
          raw["referenced_message"].is_object()) {
        auto ref = raw["referenced_message"];
        row["reply_preview"] =
            str(ref.value("author", Json::object()), "username") + ": " +
            str(ref, "content");
      }
      list.push_back(std::move(row));
    }
    p->post({{"type", "history"},
             {"channel", channel},
             {"items", list},
             {"older", !before.empty()}});
  });
}
void Backend::send(std::string channel, std::string content, std::string reply,
                   uint64_t request, std::string nonce) {
  auto *p = impl_.get();
  p->enqueue(
      "send:" + std::to_string(request),
      [p, channel, content, reply, request, nonce] {
        p->require_write();
        discord::MessageOptions o;
        if (!nonce.empty())
          o.nonce = nonce;
        o.content = content;
        if (!reply.empty())
          o.reply_to = discord::Snowflake(reply);
        auto row = message_json(p->api.send(discord::Snowflake(channel), o));
        if (!reply.empty())
          row["reply_to"] = reply;
        p->post({{"type", "sent"}, {"request", request}, {"data", row}});
      },
      request);
}
void Backend::edit(std::string channel, std::string id, std::string content,
                   uint64_t request) {
  auto *p = impl_.get();
  p->enqueue(
      "edit:" + id,
      [p, channel, id, content, request] {
        p->require_write();
        auto m = p->api.channel(discord::Snowflake(channel))
                     .fetch_message(discord::Snowflake(id));
        if (m.author.id.str() != p->me)
          throw discord::Error("Yalnızca kendi mesajını düzenleyebilirsin.");
        auto updated = m.edit(content);
        p->post({{"type", "sent"},
                 {"request", request},
                 {"data", message_json(updated)}});
      },
      request);
}
void Backend::remove(std::string channel, std::string id) {
  auto *p = impl_.get();
  p->enqueue("delete:" + id, [p, channel, id] {
    p->require_write();
    auto m = p->api.channel(discord::Snowflake(channel))
                 .fetch_message(discord::Snowflake(id));
    if (m.author.id.str() != p->me)
      throw discord::Error("Yalnızca kendi mesajını silebilirsin.");
    m.remove();
    p->post({{"type", "message_delete"}, {"channel", channel}, {"id", id}});
    p->post({{"type", "notice"}, {"message", "Mesaj silindi."}});
  });
}
void Backend::react(std::string channel, std::string id, std::string emoji) {
  auto *p = impl_.get();
  p->enqueue("react:" + id, [p, channel, id, emoji] {
    p->require_write();
    p->api.channel(discord::Snowflake(channel))
        .fetch_message(discord::Snowflake(id))
        .react(emoji);
    p->post({{"type", "notice"}, {"message", "Tepki eklendi."}});
  });
}
void Backend::dm(std::string user) {
  auto *p = impl_.get();
  p->enqueue("dm:" + user, [p, user] {
    p->require_write();
    auto c = p->api.create_dm(discord::Snowflake(user));
    p->post({{"type", "dm_open"}, {"data", channel_json(c)}});
  });
}
void Backend::profile(std::string user, std::string guild) {
  auto *p = impl_.get();
  p->enqueue("profile:" + user, [p, user, guild] {
    auto r =
        p->api.fetch_profile(discord::Snowflake(user),
                             guild.empty() ? std::optional<discord::Snowflake>{}
                                           : discord::Snowflake(guild));
    p->post({{"type", "profile"},
             {"data",
              {{"user", user_json(r.user)},
               {"bio", r.bio.value_or("")},
               {"pronouns", r.pronouns.value_or("")},
               {"mutual", r.mutual_friends_count ? Json(*r.mutual_friends_count)
                                                 : Json(nullptr)}}}});
  });
}
void Backend::join(std::string guild, std::string channel) {
  auto *p = impl_.get();
  p->enqueue("voice_join",
             [p, guild, channel] { p->start_media(guild, channel); });
}
void Backend::media(std::string action, Json data) {
  auto *p = impl_.get();
  p->enqueue("media:" + action, [p, action, data] {
    if (action == "leave") {
      p->current()->stop();
      return;
    }
    if (action == "mode") {
      {
        std::lock_guard lock(p->media_mutex);
        p->media_options.camera = data.value("camera", true);
        p->media_options.go_live = data.value("go_live", true);
      }
      if (p->media_alive)
        p->current()->set_outputs(data.value("camera", true),
                                  data.value("go_live", true));
      else
        p->post({{"type", "media_preferences"},
                 {"data",
                  {{"camera", data.value("camera", true)},
                   {"go_live", data.value("go_live", true)}}}});
      return;
    }
    if (!p->media_alive &&
        (action == "sources" || action == "mute" || action == "playback")) {
      Json preferences = Json::object();
      {
        std::lock_guard lock(p->media_mutex);
        if (action == "sources") {
          p->media_options.video_source = str(data, "video");
          p->media_options.audio_source = str(data, "audio");
          preferences["video_source"] = p->media_options.video_source;
          preferences["audio_source"] = p->media_options.audio_source;
        } else if (action == "mute") {
          p->media_options.muted = data.at("value").get<bool>();
          preferences["muted"] = p->media_options.muted;
        } else {
          p->media_options.play_received_audio = data.at("value").get<bool>();
          preferences["play_received_audio"] =
              p->media_options.play_received_audio;
        }
      }
      p->post({{"type", "media_preferences"}, {"data", preferences}});
      return;
    }
    auto session = p->current();
    if (action == "deaf")
      session->set_deaf(data.at("value").get<bool>());
    else if (action == "mute")
      session->set_muted(data.at("value").get<bool>());
    else if (action == "playback")
      session->set_audio_playback(data.at("value").get<bool>());
    else if (action == "sources")
      session->set_sources(str(data, "video"), str(data, "audio"));
    else if (action == "record")
      session->record(p->recordings / str(data, "name"),
                      data.value("audio", 20), data.value("video", 10));
    else if (action == "stop_record")
      session->stop_recording();
    else if (action == "watch")
      session->watch_stream(str(data, "key"));
    else if (action == "unwatch")
      session->unwatch_stream(str(data, "key"));
  });
}
} // namespace nova
