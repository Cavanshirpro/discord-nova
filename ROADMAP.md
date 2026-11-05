# Roadmap

The long-term target is a complete native C++ Discord client. GUI and TUI remain
entry points into discord.cpp-self. Development adopts the original, richer TUI;
the Qt GUI currently has a smaller feature set.

## v0.1.0 — First text client

- [x] GUI and TUI, shared session/message model.
- [x] Servers, text channels, existing DMs, latest history and text sending.
- [x] Live creates/edits/deletes and manual refresh.
- [x] Demo mode, offline REST/Gateway fixtures, multi-platform build/release CI.

## Next text-client milestones

- Bring the original TUI's replies, edits/deletes, reactions, new DMs, friends,
  pagination and local search to the GUI through a shared backend.
- Attachment upload, synchronized unread state and notifications.
- Rich text, links, emoji, avatars, accessibility and responsive terminal layouts.
- Better session recovery and optional OS keychain storage.
- Expand typed library coverage where the client needs it.

## Media

- Validate and package the original TUI's native-media backend on more toolchains.
- Extend its voice, video, screen sharing and recording flows to the GUI.
- Permission handling, device testing and platform-specific media packaging.

## Plugins (after the core is established)

- Define scope, lifecycle, permissions and version compatibility first.
- Add the plugin API and loader in a later release.
- Do not promise ABI compatibility before a versioned interface exists.

These are development directions, not dated delivery commitments.
