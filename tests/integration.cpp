#include <nova/core.hpp>
#include <iostream>
#include <stdexcept>

static nova::State wait(nova::Service& service, const std::function<bool(const nova::State&)>& ready) {
    for (int i = 0; i < 1500; ++i) {
        auto state = service.snapshot();
        if (ready(state)) return state;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto s = service.snapshot();
    throw std::runtime_error("Timed out: " + s.status + " " + s.error);
}
static nova::State idle(nova::Service& s) { return wait(s, [](const auto& v) { return !v.busy; }); }
static void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        nova::Options options;
        options.transport.api_url = argv[1]; options.transport.gateway_url = argv[2];
        options.transport.request_timeout = std::chrono::seconds(3);
        options.transport.max_reconnect_attempts = 0;
        nova::Service service(options);
        service.connect("offline-token");
        auto s = wait(service, [](const auto& v) { return !v.busy && v.live; });
        require(s.connected && s.user == "Fixture", "REST account login");
        service.select_guild("400"); s = idle(service);
        require(s.channels.size() == 1 && s.channels.front().id == "200", "Filter voice/category channels");
        service.select_channel("200");
        s = wait(service, [](const auto& v) { return !v.busy && v.messages.size() == 2 && v.messages.front().content == "edited live" && v.messages.back().id == "903"; });
        require(s.messages.front().edited, "Edit event during history request must survive");
        service.refresh(); s = idle(service);
        require(s.messages.size() == 1 && s.messages.front().content == "fresh REST", "Refresh must replace stale local content and deletions");
        service.send("hello @everyone"); s = idle(service);
        require(s.error.empty() && s.messages.back().content == "hello @everyone", "Send via real library REST");
        service.send("fail-send"); s = idle(service);
        require(!s.error.empty() && s.messages.size() == 2, "Failed send must not append a fake message");
        service.select_guild(""); s = idle(service);
        require(s.channels.size() == 1 && s.channels.front().name == "Friend", "Existing DMs and recipient display name");
        service.disconnect(); s = idle(service);
        require(!s.connected && !s.live && s.messages.empty(), "Gateway disconnect and session cleanup");
        // Exercise disconnect immediately after worker starts the Gateway.
        for (int i = 0; i < 4; ++i) { service.connect("offline-token"); idle(service); service.disconnect(); idle(service); }
        std::cout << "Loopback REST, Gateway creates/edits/deletes, failed sends and shutdown passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
