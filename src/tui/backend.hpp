#pragma once
#include <discord/discord.hpp>
#include <filesystem>
#include <memory>
namespace nova {
using Json = discord::Json;
std::string str(const Json &j, const char *key, std::string fallback = {});
Json user_json(const discord::User &user);
Json message_json(const discord::Message &message);
class Backend {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  Backend(Json config, std::string token, std::filesystem::path recording_root,
          discord::ClientOptions client_options = {});
  ~Backend();
  void start(bool gateway = true);
  void stop();
  std::vector<Json> drain();
  void refresh();
  void channels(std::string guild);
  void history(std::string channel, std::string before = {});
  void send(std::string channel, std::string content, std::string reply,
            uint64_t request, std::string nonce = {});
  void edit(std::string channel, std::string id, std::string content,
            uint64_t request);
  void remove(std::string channel, std::string id);
  void react(std::string channel, std::string id, std::string emoji);
  void dm(std::string user);
  void profile(std::string user, std::string guild);
  void join(std::string guild, std::string channel);
  void media(std::string action, Json data = Json::object());
};
} // namespace nova
