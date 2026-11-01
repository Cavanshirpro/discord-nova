#pragma once
#include <discord/client.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace nova {
struct Entry { std::string id, name; };
struct ChatMessage { std::string id, channel, author, content, timestamp; bool edited = false; };
struct State {
    std::uint64_t revision = 0;
    bool connected = false, demo = false, busy = false, live = false;
    std::string user, guild, channel, status = "Connect with a token or explore Demo.", error;
    std::vector<Entry> guilds, channels;
    std::vector<ChatMessage> messages;
};
struct Options {
    discord::ClientOptions transport;
    bool gateway = true;
};

// Strip terminal controls from remote text before handing it to either renderer.
std::string visible_text(std::string_view input);
std::string message_text(const ChatMessage& message);

// Both frontends share this serialized REST worker and bounded, thread-safe snapshot.
// No token or account data is written to disk. Failed sends retain their draft in the UI.
class Service {
public:
    explicit Service(Options options = {});
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    State snapshot() const;
    bool connect(std::string token);
    bool demo();
    bool disconnect();
    bool select_guild(std::string id);
    bool select_channel(std::string id);
    bool refresh();
    bool send(std::string content);
private:
    bool submit(std::function<void()> task);
    void work(std::stop_token stop);
    void mutate(const std::function<void(State&)>& action);
    void event(std::function<void(State&)> action);
    void reset();
    void load_channels(const std::string& guild);
    void load_history(const std::string& channel);
    void start_gateway(std::string token);
    void stop_gateway();
    Options options_;
    mutable std::mutex state_mutex_, queue_mutex_;
    State state_;
    bool loading_history_ = false;
    std::vector<std::function<void(State&)>> history_events_;
    std::condition_variable_any changed_;
    std::deque<std::function<void()>> tasks_;
    std::unique_ptr<discord::Client> rest_;
    std::unique_ptr<discord::Client> gateway_;
    std::thread gateway_thread_;
    std::atomic_bool gateway_done_{true}, gateway_stopping_{false};
    std::uint64_t demo_id_ = 100;
    std::jthread worker_; // Destroy/join before any state it touches.
};
}
