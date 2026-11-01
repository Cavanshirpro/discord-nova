# Roadmap

The long-term target is a complete native C++ Discord client. GUI and TUI remain
equal entry points into the same core; networking belongs in discord.cpp-self.

## v0.1.0 — First text client

- [x] GUI and TUI, shared session/message model.
- [x] Servers, text channels, existing DMs, latest history and text sending.
- [x] Live creates/edits/deletes and manual refresh.
- [x] Demo mode, offline REST/Gateway fixtures, multi-platform build/release CI.

## Next text-client milestones

- Start DMs, replies, client-side edits/deletes, reactions and attachments.
- Older-message pagination, search, unread state and notifications.
- Rich text, links, emoji, avatars, accessibility and responsive terminal layouts.
- Better session recovery and optional OS keychain storage.
- Expand typed library coverage where the client needs it.

## Media

- Voice channels, microphone/device controls and mute/deafen.
- Video, screen sharing, media receive/playback through discord.cpp-self.
- Permission handling, device testing and platform-specific media packaging.

## Plugins (after the core is established)

- Define scope, lifecycle, permissions and version compatibility first.
- Add the plugin API and loader in a later release.
- Do not promise ABI compatibility before a versioned interface exists.

These are development directions, not dated delivery commitments.
