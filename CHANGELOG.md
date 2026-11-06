# Changelog

## Unreleased

- Fix server/channel loading with compressed REST responses in browser profiles.
- Show channel history permission failures separately from connection failures.
- Cache wrapped chat lines, redraw only on changes, and avoid duplicate SDK history.
- Bound offscreen event bookkeeping and publish validated Windows TUI bundles
  through a latest-version launcher pointer.
- Keep older history pages at the 500-message limit, evict least recently used
  channel histories, and refresh bootstrap reads after Gateway readiness/resume.

- Adopt the original Nova TUI from discord.cpp-mytesting: preserve its server rail,
  editor, replies/edits/reactions, friends, drafts and voice/media controls.
- Add POSIX terminal key/UTF-8/mouse/paste decoding and local backend fixtures.
- Add native-media source-build support and Windows launchers for the original TUI.
- Preserve browser/CA settings across the independent media Gateway and voice/stream WebSockets.

- Embed a separate Qt QSS theme with profile selector, focus, hover and disabled states.

- Chrome 152 TLS/HTTP browser profile in both frontends, backed by a pinned
  curl-impersonate SDK through discord.cpp-self.
- Profile selection, explicit trust bundles, and local ClientHello/HTTP2/HTTPS/WSS tests.

## 0.1.0 — 2026-11-01

- First native C++20 text-client release with Qt GUI and FTXUI TUI.
- Shared asynchronous session core using discord.cpp-self v0.2.1.
- Servers, text channels, existing DMs, history, sends and live message updates.
- Offline demo mode and HTTP/WebSocket integration fixtures.
- Windows MSVC/MINGW64/UCRT64/CLANG64, Linux and macOS build/release automation.
- Runtime packaging, source snapshot, notices and SHA-256 checksums.

Plugins, media and full Discord feature parity remain planned.
