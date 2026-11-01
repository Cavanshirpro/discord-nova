#include <nova/core.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <cstdlib>
#include <algorithm>
#include <iostream>

int main(int argc, char** argv) {
    bool demo = false, smoke = false;
    auto options = nova::frontend_options();
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--version") { std::cout << "discord-nova " NOVA_VERSION "\n"; return 0; }
        if (arg == "--help" || arg == "-h") {
            std::cout << "discord-nova-tui [--demo] [--version] [--browser-profile chrome152|default]\n"
                         "Tab / Shift+Tab: focus, arrows: select, Enter: activate/send, Ctrl+Q: quit.\n"
                         "DISCORD_TOKEN can prefill the masked token field. Tokens are never saved.\n";
            return 0;
        }
        if (arg == "--demo") demo = true;
        else if (arg == "--smoke-test") smoke = true;
        else if (arg == "--browser-profile" && i + 1 < argc) {
            options.transport.browser_profile = argv[++i];
            if (options.transport.browser_profile == "default") options.transport.browser_profile.clear();
        }
        else { std::cerr << "Unknown argument. See --help.\n"; return 2; }
    }
    nova::Service service(options);
    if (demo || smoke) service.demo();
    if (smoke) {
        for (int i = 0; i < 200; ++i) {
            const auto s = service.snapshot();
            if (!s.busy && s.demo && s.messages.size() == 3) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (service.snapshot().busy || service.snapshot().messages.size() != 3) return 1;
    }
    using namespace ftxui;
    auto screen = ScreenInteractive::Fullscreen();
    std::string token, draft, pending_draft;
    bool sending = false;
    if (const auto* value = std::getenv("DISCORD_TOKEN")) token = value;
    InputOption password; password.password = true;
    std::vector<std::string> profile_names = {"Default TLS"}, profile_values = {""};
    int profile_index = 0;
    if (discord::supports_browser_impersonation()) { profile_names.push_back("Chrome 152"); profile_values.push_back("chrome152"); }
    if (!options.transport.browser_profile.empty()) {
        auto found = std::find(profile_values.begin(), profile_values.end(), options.transport.browser_profile);
        if (found == profile_values.end()) { profile_names.push_back(nova::visible_text(options.transport.browser_profile)); profile_values.push_back(options.transport.browser_profile); }
        profile_index = static_cast<int>(std::find(profile_values.begin(), profile_values.end(), options.transport.browser_profile) - profile_values.begin());
    }
    auto profile = Toggle(&profile_names, &profile_index);
    auto token_input = Input(&token, "Account token (memory only)", password);
    auto connect = Button("Connect", [&] { if (service.connect(token, profile_values[static_cast<std::size_t>(profile_index)])) token.clear(); });
    auto try_demo = Button("Demo", [&] { service.demo(); token.clear(); });
    auto disconnect = Button("Disconnect", [&] { service.disconnect(); });
    auto refresh = Button("Refresh", [&] { service.refresh(); });
    auto quit = Button("Quit", screen.ExitLoopClosure());
    auto auth = Container::Horizontal({token_input, connect, try_demo, disconnect, refresh, quit});
    std::vector<std::string> guild_names, channel_names;
    std::vector<nova::Entry> guild_entries, channel_entries;
    int guild_index = 0, channel_index = 0;
    auto guild_options = MenuOption::Vertical();
    guild_options.on_enter = [&] {
        if (guild_index >= 0 && guild_index < static_cast<int>(guild_entries.size())) service.select_guild(guild_entries[static_cast<std::size_t>(guild_index)].id);
    };
    auto channel_options = MenuOption::Vertical();
    channel_options.on_enter = [&] {
        if (channel_index >= 0 && channel_index < static_cast<int>(channel_entries.size())) service.select_channel(channel_entries[static_cast<std::size_t>(channel_index)].id);
    };
    auto guilds = Menu(&guild_names, &guild_index, guild_options);
    auto channels = Menu(&channel_names, &channel_index, channel_options);
    auto send_message = [&] {
        if (service.send(draft)) { sending = true; pending_draft = draft; }
    };
    InputOption compose; compose.on_enter = send_message;
    auto input = Input(&draft, "Write a message...", compose);
    auto send = Button("Send", send_message);
    auto composer = Container::Horizontal({input, send});
    int message_index = 0;
    std::vector<std::string> message_lines;
    auto history = Menu(&message_lines, &message_index);
    auto body = Container::Horizontal({guilds, channels, history});
    auto all = Container::Vertical({auth, profile, body, composer});
    nova::State state;
    std::uint64_t revision = static_cast<std::uint64_t>(-1);
    auto renderer = Renderer(all, [&] {
        state = service.snapshot();
        if (revision != state.revision) {
            revision = state.revision;
            auto update_entries = [](const auto& entries, auto& cached, auto& labels, int& index) {
                bool same = entries.size() == cached.size();
                for (std::size_t i = 0; same && i < entries.size(); ++i) same = entries[i].id == cached[i].id && entries[i].name == cached[i].name;
                if (same) return;
                cached = entries; labels.clear(); index = 0;
                for (const auto& e : entries) labels.push_back(nova::visible_text(e.name));
            };
            update_entries(state.guilds, guild_entries, guild_names, guild_index);
            update_entries(state.channels, channel_entries, channel_names, channel_index);
            std::vector<std::string> next;
            for (const auto& m : state.messages) {
                // Each message is focusable so arrows/PageUp/PageDown can scroll history.
                auto line = nova::visible_text(m.author) + ": " + nova::visible_text(m.content);
                std::replace(line.begin(), line.end(), '\n', ' ');
                if (m.edited) line += " (edited)";
                next.push_back(std::move(line));
            }
            if (next != message_lines) {
                const bool bottom = message_index >= static_cast<int>(message_lines.size()) - 1;
                message_lines = std::move(next);
                if (bottom) message_index = std::max(0, static_cast<int>(message_lines.size()) - 1);
            }
            if (sending && !state.busy) {
                if (state.error.empty() && draft == pending_draft) draft.clear();
                pending_draft.clear(); sending = false;
            }
        }
        auto title = hbox({text(" NOVA ") | bold | color(Color::Cyan), text("v" NOVA_VERSION), filler(),
                           text(nova::visible_text(state.user) + (state.demo ? "  DEMO " : state.live ? "  LIVE " : "  REST ") + (state.connected && !state.demo ? (state.browser_profile.empty() ? "default TLS" : nova::visible_text(state.browser_profile)) : ""))});
        auto columns = hbox({vbox({text("SERVERS / DMs") | bold, separator(), guilds->Render() | vscroll_indicator | yframe | flex}) | size(WIDTH, EQUAL, 23) | border,
                             vbox({text("CHANNELS") | bold, separator(), channels->Render() | vscroll_indicator | yframe | flex}) | size(WIDTH, EQUAL, 25) | border,
                             vbox({text("CONVERSATION") | bold, separator(), history->Render() | vscroll_indicator | yframe | flex}) | flex | border}) | flex;
        return vbox({title, auth->Render() | border, hbox({text(" Next connection: "), profile->Render(), text(" | TLS verification on") | dim}), columns,
                     hbox({input->Render() | flex, send->Render()}) | border,
                     paragraph(nova::visible_text(state.error.empty() ? state.status : state.error)) | color(state.error.empty() ? Color::GrayLight : Color::RedLight),
                     text(state.busy ? " Working..." : " Tab: focus | Arrows: select/scroll | Enter: activate/send | Ctrl+Q: quit") | dim}) | flex;
    });
    auto events = CatchEvent(renderer, [&](Event e) {
        if (e == Event::CtrlQ) { screen.ExitLoopClosure()(); return true; }
        // Keep a submitted draft unchanged until the send succeeds or fails.
        if (service.snapshot().busy && e != Event::Custom && (input->Focused() || send->Focused())) return true;
        return false;
    });
    if (smoke) {
        auto buffer = Screen::Create(Dimension::Fixed(120), Dimension::Fixed(30));
        Render(buffer, renderer->Render());
        const auto output = buffer.ToString();
        if (output.find("NOVA") == std::string::npos || output.find("Direct Messages") == std::string::npos || output.find("Welcome to discord-nova") == std::string::npos) {
            std::cerr << "TUI demo render failed\n" << output << '\n'; return 1;
        }
        std::cout << "TUI demo rendered at 120x30\n";
        return 0;
    }
    std::jthread ticker([&](std::stop_token stop) {
        while (!stop.stop_requested()) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); screen.PostEvent(Event::Custom); }
    });
    screen.Loop(events);
    ticker.request_stop(); ticker.join();
    return 0;
}
