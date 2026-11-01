#include <nova/core.hpp>
#include <iostream>
#include <stdexcept>

static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static nova::State idle(nova::Service& service) {
    for (int i = 0; i < 500; ++i) {
        auto s = service.snapshot();
        if (!s.busy) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Worker did not become idle");
}
int main() {
    try {
        require(nova::visible_text("hello\x1b[31m\a\r\nworld") == "hello[31m\nworld", "Remote controls must be inert");
        require(nova::visible_text("Türkçe 🌟") == "Türkçe 🌟", "UTF-8 must survive rendering");
        nova::Service service;
        {
            discord::ClientOptions identity;
            identity.browser_profile = "unsupported_profile";
            bool rejected = false;
            try { discord::Client client(identity); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Unsupported profiles must fail before authentication");
            identity.browser_profile = "chrome152";
            if (discord::supports_browser_impersonation()) { discord::Client client(identity); }
            else {
                rejected = false;
                try { discord::Client client(identity); } catch (const std::invalid_argument&) { rejected = true; }
                require(rejected, "System curl must not silently simulate impersonation");
            }
        }
        require(!service.send("offline"), "Cannot send offline");
        require(!service.connect(""), "Reject empty token");
        require(service.demo(), "Demo accepted");
        auto s = idle(service);
        require(s.demo && s.connected && s.channel == "10" && s.messages.size() == 3, "Demo starts with a channel");
        require(!service.send(" \n\t"), "Reject blank message");
        require(!service.send(std::string(2001, 'x')), "Enforce conservative byte limit");
        require(service.send("Türkçe <b>plain text</b>"), "Send demo message");
        s = idle(service);
        require(s.messages.size() == 4 && s.messages.back().author == "Explorer", "Demo appends local message");
        service.refresh(); s = idle(service);
        require(s.messages.size() == 4, "Refresh retains demo sends");
        service.select_guild(""); s = idle(service);
        require(s.channels.size() == 1 && s.channel.empty() && s.messages.empty(), "DM list clears previous conversation");
        service.select_channel("30"); s = idle(service);
        require(s.channel == "30" && s.messages.size() == 3, "DM history");
        service.disconnect(); s = idle(service);
        require(!s.connected && s.user.empty() && s.messages.empty() && s.guilds.empty(), "Disconnect clears session data");
        for (int i = 0; i < 5; ++i) { service.demo(); idle(service); service.disconnect(); idle(service); }
        std::cout << "Core demo, validation, UTF-8 and session lifecycle passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
