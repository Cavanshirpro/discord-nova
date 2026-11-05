#include "native_voice.hpp"
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  try {
    discord::detail::init_curl();
    discord::ClientOptions transport;
    if (discord::supports_browser_impersonation()) transport.browser_profile = "chrome152";
    transport.ca_bundle = argv[2];
    std::atomic_bool stop = false;
    try {
      discord::detail::media::SocketWeb socket(argv[1], stop, 2000, transport);
      if (std::string_view(argv[3]) == "reject") throw std::runtime_error("Untrusted voice TLS succeeded");
      socket.send(discord::Json{{"fixture", "media"}});
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      while (std::chrono::steady_clock::now() < deadline) {
        if (auto packet = socket.receive()) {
          auto json = discord::Json::parse(packet->first.begin(), packet->first.end());
          if (json.value("fixture", "") != "media") throw std::runtime_error("Unexpected voice fixture reply");
          return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      throw std::runtime_error("Voice fixture reply timed out");
    } catch (const discord::TransportError&) {
      if (std::string_view(argv[3]) != "reject") throw;
    }
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
