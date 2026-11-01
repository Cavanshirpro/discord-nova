# discord-nova v0.1.0

First public release of discord-nova: a native C++20 Discord text client with a
Qt Widgets desktop GUI and an FTXUI terminal interface, powered by our own
discord.cpp-self v0.2.1.

## Included

- Account login, servers, text channels and existing direct/group conversations.
- Latest 100 messages, text sending, manual refresh and live creates/edits/deletes.
- Shared background REST worker and Gateway session; account data stays in memory.
- Offline Demo in both interfaces. No account needed to explore.
- Offline core, REST/Gateway integration and frontend startup tests.

## Downloads

Choose the matching archive: Windows x64 MSVC (usual desktop choice), MINGW64,
UCRT64 or CLANG64; Linux x64/ARM64; macOS x64/ARM64. Keep all extracted files
together. `source.zip` needs the dependencies and Internet access on first build.
`SHA256SUMS` and per-archive checksum files verify the downloads.

Windows bundles third-party DLLs and Qt platform plugins. Linux packages target
Ubuntu 24.04+ and require system Qt/OpenSSL/zlib (`libqt6widgets6 libqt6gui6
libqt6core6t64 libssl3t64 zlib1g`). macOS 15+ bundles runtime libraries and uses ad-hoc
signing, without Apple notarization. See the README for launch commands.

## Current scope

This is an early text client, not yet a feature-complete Discord replacement.
No plugins, voice/video, screen sharing, uploads, rich Markdown or notifications.
Existing DMs only; history is one page. The single-line composer caps messages
at 2000 UTF-8 bytes. Gateway failure leaves manual Refresh available.

CI tests use local fixtures and fake tokens. Real account login and permissions
remain dependent on Discord's unofficial API. No real account token is used in CI.

Source and build instructions: https://github.com/Cavanshirpro/discord-nova
