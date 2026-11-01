# discord-nova

[![Build](https://github.com/Cavanshirpro/discord-nova/actions/workflows/ci.yml/badge.svg)](https://github.com/Cavanshirpro/discord-nova/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Cavanshirpro/discord-nova)](https://github.com/Cavanshirpro/discord-nova/releases)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue.svg)](LICENSE)

A native C++ Discord client with a **desktop GUI** and a **terminal TUI**, built on
our own [discord.cpp-self](https://github.com/Cavanshirpro/discord.cpp-self).
A personal project by [Cavanshirpro](https://github.com/Cavanshirpro).

The goal is a complete C++ alternative to the Discord client. **v0.1.0 is the
first text-client release**, with shared behavior across both interfaces.
It is an early foundation; full Discord feature parity is still ahead.

**[Download v0.1.0](https://github.com/Cavanshirpro/discord-nova/releases/tag/v0.1.0)**
| [Build guide](docs/BUILDING.md) | [Türkçe](docs/README.tr.md)
| [Roadmap](ROADMAP.md) | [Release notes](release.md)

Development builds additionally support [Chrome browser/TLS identity](docs/BROWSER_IDENTITY.md).
Published v0.1.0 binaries predate that feature.

## What works

- Qt Widgets desktop GUI and FTXUI keyboard/mouse terminal interface.
- Account login, server lists, text channels and existing direct/group messages.
- Latest 100 messages, manual refresh, text sending with mentions disabled.
- Live Gateway message creation, edits and deletions, reconnect/resume via the library.
- Background REST work, bounded message history and clean session shutdown.
- Offline demo mode: explore either interface without an account.
- Windows, Linux and macOS builds; x64 and selected ARM64 binaries.

Tokens and messages stay in process memory. No account token, message history,
analytics or configuration is saved to disk. A failed send keeps the draft.
Connect explicitly; setting `DISCORD_TOKEN` only prefills the masked token field.

## Try it

Extract the matching release archive. On Windows the MSVC build is the usual
desktop choice; the MINGW64, UCRT64 and CLANG64 archives are alternative native
toolchain builds. Keep the entire extracted folder together.

```powershell
# Windows PowerShell (CMD can run the same executables)
.\bin\discord-nova-gui.exe --demo
.\bin\discord-nova-tui.exe --demo
```

```sh
# Linux / macOS terminal
./bin/discord-nova-tui --demo
# Linux GUI
./bin/discord-nova-gui --demo
# macOS GUI
open discord-nova-gui.app --args --demo
```

Linux archives target **Ubuntu 24.04 or newer** on the matching architecture and
use system Qt/OpenSSL libraries. Install runtime dependencies first:

```sh
sudo apt-get install libqt6widgets6 libqt6gui6 libqt6core6t64 libssl3t64 zlib1g
```

macOS archives target macOS 15+, bundle Qt and Homebrew runtime libraries and are ad-hoc signed;
they are not Apple-notarized. Windows archives include Qt plugins and required
third-party DLLs. MSYS2 archives also include curl's CA certificate bundle in
`etc/ssl/certs/`, preserving its relocated trust-store path. Release packages
run a staged demo smoke test before upload.
Use the matching architecture; these are desktop builds, not Android/iOS apps.

For your account, launch either program, enter a token in its masked field and
choose **Connect**. Do not paste tokens into issues, screenshots or command-line
arguments. You can also prefill with the `DISCORD_TOKEN` process environment
variable. Discord's unofficial user API can change and can require verification;
the client does not bypass those challenges. This project is not affiliated with Discord.

In the TUI use **Tab / Shift+Tab** to move focus, **arrows** to select/scroll,
**Enter** to open a server/channel or send, and **Ctrl+Q** to quit. Use a modern
UTF-8 terminal, preferably at least 100×28 cells. PowerShell, CMD and Git Bash
are shells: the same Windows binaries run from each; no separate binary is
needed for each terminal emulator.

## Build from source

Requirements: C++20, CMake 3.25+, Ninja, Python 3.12+ for tooling, Qt 6.4+
(GUI), curl 7.86+ with **ws/wss**, nlohmann/json 3.11+ and zlib. FTXUI and
discord.cpp-self are downloaded at pinned commits by CMake. Initial configuration
needs Git and network access. Dependency versions are listed in [THIRD_PARTY.md](THIRD_PARTY.md).

```sh
git clone https://github.com/Cavanshirpro/discord-nova.git
cd discord-nova
```

| Environment | Build command |
| --- | --- |
| Windows PowerShell | `./scripts/build.ps1 -Toolchain mingw64` |
| Windows CMD | `scripts\build.cmd -Toolchain mingw64` |
| Visual Studio x64 Developer shell | `scripts\build.cmd -Toolchain msvc` |
| MSYS2 MINGW64 / UCRT64 / CLANG64 | `sh scripts/build.sh` |
| Linux bash / zsh / fish / dash | `sh scripts/build.sh --preset linux` |
| macOS Terminal / iTerm | `sh scripts/build.sh --preset macos` |

See [BUILDING.md](docs/BUILDING.md) for dependency commands, custom MSYS2
locations, TUI-only builds, packaging, local library development and CI details.

## Scope of v0.1

Text-only, plain-text rendering. No voice/video, screen sharing, upload, media
player, Markdown renderer, notifications, search, login QR flow, or client-side
message editing/deletion yet. Live edits/deletions made elsewhere are displayed.
The composer currently accepts one line and caps messages at 2000 UTF-8 bytes.
Only the latest 100 messages are fetched; live history is capped at 200. Existing
DMs are listed, but starting a new DM is not exposed yet. Threads appear only
when returned by the library's channel list. Forbidden channels report an error.

**Plugins are planned and intentionally absent from v0.1.** The shared core keeps
the frontends independent; no plugin ABI or loader is promised yet.

## Development and releases

`ci.yml` builds and tests Linux x64/ARM64, macOS x64/ARM64, and Windows x64
MSVC/MINGW64/UCRT64/CLANG64. `release.yml` runs the same matrix for a version tag
and publishes the binaries, source snapshot and SHA-256 checksums **only after
every job succeeds**. Release metadata must agree across `VERSION`, CMake,
`vcpkg.json`, `release.json` and `release.md`.

Tests use demo data and local HTTP/WebSocket fixtures; CI never needs a real
Discord token. Read [CONTRIBUTING.md](CONTRIBUTING.md) before contributing and
[SECURITY.md](SECURITY.md) for private vulnerability reporting. Library limitations
are tracked in [docs/LIBRARY.md](docs/LIBRARY.md).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE), [NOTICE](NOTICE) and
[THIRD_PARTY.md](THIRD_PARTY.md) for dependency licenses and source links.
