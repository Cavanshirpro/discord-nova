#include <nova/core.hpp>
#include <algorithm>
#include <stdexcept>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace nova {
Options frontend_options() {
    Options result;
    if (discord::supports_browser_impersonation()) result.transport.browser_profile = "chrome152";
    if (const char* profile = std::getenv("NOVA_BROWSER_PROFILE"))
        result.transport.browser_profile = std::string_view(profile) == "default" ? "" : profile;
    if (const char* bundle = std::getenv("NOVA_CA_BUNDLE")) {
        result.transport.ca_bundle = bundle;
    } else {
        std::filesystem::path executable;
#ifdef _WIN32
        std::wstring buffer(32768, L'\0');
        const auto count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (count && count < buffer.size()) { buffer.resize(count); executable = buffer; }
#elif defined(__APPLE__)
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) executable = std::filesystem::weakly_canonical(buffer.c_str());
#elif defined(__linux__)
        std::error_code error;
        executable = std::filesystem::read_symlink("/proc/self/exe", error);
#endif
        if (!executable.empty()) {
            auto bundle = executable.parent_path() / "cacert.pem";
#ifdef __APPLE__
            // App resources belong outside MacOS, where deployment tools expect binaries.
            if (executable.parent_path().filename() == "MacOS")
                bundle = executable.parent_path().parent_path() / "Resources/cacert.pem";
#endif
            if (std::filesystem::is_regular_file(bundle)) result.transport.ca_bundle = bundle.string();
        }
    }
    return result;
}
std::string visible_text(std::string_view input) {
    std::string out;
    for (unsigned char c : input) {
        if (c == '\n' || c == '\t' || (c >= 32 && c != 127)) out += static_cast<char>(c);
    }
    return out;
}
std::string message_text(const ChatMessage& m) {
    return visible_text(m.author) + (m.edited ? " (edited)" : "") + "\n" + visible_text(m.content);
}
static ChatMessage convert(const discord::Message& m) {
    auto content = m.content;
    for (const auto& a : m.attachments) content += "\n[Attachment] " + a.filename;
    return {m.id.str(), m.channel_id.str(), m.author.display_name.empty() ? m.author.name : m.author.display_name,
            std::move(content), m.timestamp, m.edited_timestamp.has_value()};
}
static void upsert(State& s, ChatMessage m) {
    if (m.channel != s.channel) return;
    auto it = std::find_if(s.messages.begin(), s.messages.end(), [&](const auto& old) { return old.id == m.id; });
    if (it != s.messages.end()) *it = std::move(m); else s.messages.push_back(std::move(m));
    // Snowflakes sort numerically by decimal length and then lexicographically.
    std::sort(s.messages.begin(), s.messages.end(), [](const auto& a, const auto& b) {
        return a.id.size() == b.id.size() ? a.id < b.id : a.id.size() < b.id.size();
    });
    if (s.messages.size() > 200) s.messages.erase(s.messages.begin(), s.messages.end() - 200);
}
Service::Service(Options options) : options_(std::move(options)), worker_([this](auto stop) { work(stop); }) {}
Service::~Service() {
    worker_.request_stop();
    changed_.notify_all();
    worker_.join();
}
State Service::snapshot() const { std::lock_guard lock(state_mutex_); return state_; }
void Service::mutate(const std::function<void(State&)>& action) {
    std::lock_guard lock(state_mutex_); action(state_); ++state_.revision;
}
void Service::event(std::function<void(State&)> action) {
    std::lock_guard lock(state_mutex_);
    if (loading_history_) {
        if (history_events_.size() < 4096) history_events_.push_back(std::move(action));
        else state_.error = "Live updates overflowed while loading history. Please Refresh.";
    } else action(state_);
    ++state_.revision;
}
bool Service::submit(std::function<void()> task) {
    {
        std::lock_guard lock(state_mutex_);
        if (state_.busy) return false;
        state_.busy = true; state_.error.clear(); ++state_.revision;
    }
    { std::lock_guard lock(queue_mutex_); tasks_.push_back(std::move(task)); }
    changed_.notify_one();
    return true;
}
void Service::work(std::stop_token stop) {
    while (!stop.stop_requested()) {
        std::function<void()> task;
        {
            std::unique_lock lock(queue_mutex_);
            changed_.wait(lock, stop, [&] { return !tasks_.empty(); });
            if (stop.stop_requested()) break;
            task = std::move(tasks_.front()); tasks_.pop_front();
        }
        try { task(); }
        catch (const std::exception& e) {
            // Transport diagnostics can contain remote content; never include the token ourselves.
            mutate([&](State& s) { s.error = visible_text(e.what()); s.status = "Request failed. You can retry."; });
        }
        mutate([](State& s) { s.busy = false; });
    }
    stop_gateway(); rest_.reset();
}
void Service::stop_gateway() {
    gateway_stopping_ = true;
    // run() resets the library stop flag during startup. Repeat close until run exits,
    // including a disconnect requested immediately after starting its thread.
    while (!gateway_done_) {
        gateway_->close();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (gateway_thread_.joinable()) gateway_thread_.join();
    gateway_.reset();
}
void Service::reset() {
    stop_gateway(); rest_.reset();
    mutate([](State& s) { const auto rev = s.revision; s = {}; s.revision = rev; s.busy = true; });
}
bool Service::connect(std::string token, std::optional<std::string> browser_profile) {
    if (token.empty()) { mutate([](State& s) { s.error = "Enter a token or choose Demo."; }); return false; }
    return submit([this, token = std::move(token), browser_profile = std::move(browser_profile)]() mutable {
        reset();
        auto transport = options_.transport;
        if (browser_profile) transport.browser_profile = *browser_profile;
        rest_ = std::make_unique<discord::Client>(transport);
        rest_->login(token);
        const auto user = rest_->fetch_me();
        const auto guilds = rest_->fetch_guilds();
        mutate([&](State& s) {
            s.connected = true; s.user = user.display_name.empty() ? user.name : user.display_name;
            s.browser_profile = transport.browser_profile;
            s.guilds = {{"", "Direct Messages"}};
            for (const auto& g : guilds) s.guilds.push_back({g.id.str(), g.name});
            s.status = "Connected. Select a server or Direct Messages.";
        });
        if (options_.gateway) start_gateway(std::move(token), std::move(transport));
    });
}
bool Service::demo() {
    return submit([this] {
        reset();
        mutate([](State& s) {
            s.connected = true; s.demo = true; s.user = "Explorer";
            s.guilds = {{"", "Direct Messages"}, {"1", "Nova Community"}, {"2", "C++ Workshop"}};
            s.status = "Demo - local sample messages. Nothing is sent to Discord.";
        });
        load_channels("1"); load_history("10");
    });
}
bool Service::disconnect() {
    return submit([this] { reset(); mutate([](State& s) { s.status = "Disconnected. Session data cleared."; }); });
}
void Service::load_channels(const std::string& guild) {
    const auto current = snapshot();
    std::vector<Entry> entries;
    if (current.demo) {
        if (guild.empty()) entries = {{"30", "Nova Guide"}};
        else if (guild == "1") entries = {{"10", "# welcome"}, {"11", "# general"}, {"12", "# development"}};
        else entries = {{"20", "# cpp"}, {"21", "# showcase"}};
    } else {
        auto channels = guild.empty() ? rest_->request("GET", "/users/@me/channels").get<std::vector<discord::Channel>>()
                                      : rest_->fetch_channels(discord::Snowflake(guild));
        std::stable_sort(channels.begin(), channels.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
        for (const auto& c : channels) {
            if (c.type != 0 && c.type != 1 && c.type != 3 && c.type != 5 && c.type != 10 && c.type != 11 && c.type != 12) continue;
            auto name = c.name;
            if (name.empty()) for (const auto& user : c.recipients) {
                if (!name.empty()) name += ", ";
                name += user.display_name.empty() ? user.name : user.display_name;
            }
            entries.push_back({c.id.str(), (guild.empty() ? "" : "# ") + (name.empty() ? c.id.str() : name)});
        }
    }
    mutate([&](State& s) {
        s.guild = guild; s.channel.clear(); s.channels = std::move(entries); s.messages.clear();
        s.status = s.channels.empty() ? "No text channels available." : "Select a channel.";
    });
}
bool Service::select_guild(std::string id) {
    if (!snapshot().connected) return false;
    return submit([this, id = std::move(id)] { load_channels(id); });
}
void Service::load_history(const std::string& channel) {
    const auto current = snapshot();
    std::vector<ChatMessage> messages;
    if (current.demo) {
        if (channel == current.channel) messages = current.messages;
        else messages = {{"1", channel, "Nova", "Welcome to discord-nova v0.1.0!", "", false},
                         {"2", channel, "Cavanshirpro", "One C++ core. A desktop GUI and a terminal TUI.", "", false},
                         {"3", channel, "Nova", "Write a message below to try this local demo. Plugins, voice and video come later.", "", false}};
    } else {
        { std::lock_guard lock(state_mutex_); loading_history_ = true; }
        try {
            for (const auto& m : rest_->channel(discord::Snowflake(channel)).history(100)) messages.push_back(convert(m));
        } catch (...) {
            mutate([&](State& s) {
                for (auto& apply : history_events_) apply(s);
                history_events_.clear(); loading_history_ = false;
            });
            throw;
        }
    }
    mutate([&](State& s) {
        s.channel = channel; s.messages.clear();
        for (auto& m : messages) upsert(s, std::move(m));
        // Apply creates, patches and tombstones received during the REST request in order.
        for (auto& apply : history_events_) apply(s);
        history_events_.clear(); loading_history_ = false;
        s.status = s.demo ? "Demo - messages stay in memory." : "Latest 100 messages loaded. Refresh is available.";
    });
}
bool Service::select_channel(std::string id) {
    if (!snapshot().connected || id.empty()) return false;
    return submit([this, id = std::move(id)] { load_history(id); });
}
bool Service::refresh() {
    const auto s = snapshot();
    if (!s.connected) return false;
    return submit([this, s] { if (!s.channel.empty()) load_history(s.channel); else load_channels(s.guild); });
}
bool Service::send(std::string content) {
    const auto current = snapshot();
    if (!current.connected || current.channel.empty()) return false;
    if (content.find_first_not_of(" \r\n\t") == std::string::npos || content.size() > 2000) {
        mutate([](State& s) { s.error = "Message must contain text and be at most 2000 UTF-8 bytes in v0.1."; });
        return false;
    }
    return submit([this, current, content = std::move(content)] {
        ChatMessage message;
        if (current.demo) message = {std::to_string(++demo_id_), current.channel, current.user, content, "", false};
        else message = convert(rest_->send(discord::Snowflake(current.channel), content));
        mutate([&](State& s) { upsert(s, std::move(message)); s.status = "Message sent."; });
    });
}
void Service::start_gateway(std::string token, discord::ClientOptions transport) {
    gateway_ = std::make_unique<discord::Client>(std::move(transport));
    gateway_->on_ready([this](const discord::Ready&) { mutate([](State& s) { s.live = true; }); });
    gateway_->on_resumed([this] { mutate([](State& s) { s.live = true; }); });
    gateway_->on_message([this](const discord::Message& m) { event([message = convert(m)](State& s) { upsert(s, message); }); });
    gateway_->on_message_edit([this](const discord::MessageUpdate& m) {
        event([m](State& s) {
            if (m.channel_id.str() != s.channel) return;
            auto it = std::find_if(s.messages.begin(), s.messages.end(), [&](const auto& old) { return old.id == m.id.str(); });
            if (it != s.messages.end()) {
                if (m.patch.contains("content")) it->content = m.patch.at("content").get<std::string>();
                it->edited = true;
            }
        });
    });
    gateway_->on_message_delete([this](const discord::MessageDelete& m) {
        event([m](State& s) { if (m.channel_id.str() == s.channel) std::erase_if(s.messages, [&](const auto& old) { return old.id == m.id.str(); }); });
    });
    gateway_done_ = false; gateway_stopping_ = false;
    gateway_thread_ = std::thread([this, token = std::move(token)] {
        try { gateway_->run(token); }
        catch (const std::exception&) {
            if (!gateway_stopping_) mutate([](State& s) {
                s.error = "Live connection unavailable. Use Refresh to load messages, or reconnect.";
            });
        }
        mutate([](State& s) { s.live = false; });
        gateway_done_ = true;
    });
}
}
