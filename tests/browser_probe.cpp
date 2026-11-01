#include <discord/client.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    try {
        discord::ClientOptions options;
        options.api_url = argv[1]; options.ca_bundle = argv[2]; options.browser_profile = "chrome152";
        options.request_timeout = std::chrono::seconds(3);
        discord::Client client(options);
        client.login("offline-token");
        try {
            const auto first = client.fetch_me();
            const auto second = client.fetch_me();
            if (std::string_view(argv[3]) == "reject") { std::cerr << "Untrusted TLS connection succeeded\n"; return 1; }
            if (first.id.str() != "100" || second.id != first.id) return 1;
        } catch (const discord::TransportError&) {
            if (std::string_view(argv[3]) != "reject") throw;
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
