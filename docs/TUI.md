# Original Nova terminal client

Development uses the original C++ terminal application from
`discord.cpp-mytesting/discord-nova`, previously launched with `start-tui.cmd`.
Its server rail, grouped channels, conversation layout, voice/media cards,
friends, command palette, UTF-8 editor and responsive layouts are preserved in
`src/tui/`. It replaces the smaller FTXUI frontend shipped in Nova v0.1.0.
The Qt Widgets GUI retains its embedded QSS theme; its feature set is currently
smaller than this TUI. Both use discord.cpp-self for Discord communication.

Only source files and the synthetic REST fixture were imported. Personal
configuration, tokens, drafts, logs, recordings and caches are local data.

## Start

`start-demo.cmd`, or `discord-nova-tui --demo`, opens the original offline demo.
For a real session, copy `config.example.json` to `config.json` and create a
local `test_token` file containing the token. `start-tui.cmd` uses those files.
From any supported shell you can instead pass paths explicitly:

```sh
./bin/discord-nova-tui /path/to/config.json /path/to/token-file
```

Guild/channel IDs of `0` are placeholders: choose a real text channel in the UI
and explicitly select a voice channel to join. Startup never joins voice.
No token value is placed in process arguments. Chrome TLS/HTTP profiles use the
same [connection defaults](BROWSER_IDENTITY.md) as the GUI; override with
`--browser-profile chrome152|default`, `--ca-bundle roots.pem` or environment.
`--capabilities` reports browser and native-media build support without connecting.

## Keys

| Keys | Action |
| --- | --- |
| Tab / Shift+Tab, arrows, Enter | Focus and navigate servers/channels |
| Ctrl+K | Search commands, servers and channels |
| Enter / Alt+Enter | Compose/send explicitly / insert a newline |
| Ctrl+R / Ctrl+E / Ctrl+Delete | Reply / edit own message / confirm deletion |
| Ctrl+G / Ctrl+P / Ctrl+O | Reaction / profile / open message in browser |
| PgUp / PgDn, F3 | Older history and scrolling / local search |
| F4 / F5 / F6 / F7 | Media / refresh / friends / events |
| Alt+M / Alt+D / Alt+L | Outgoing mute / deafen / local playback |
| Alt+C / Alt+G | Camera / Go Live output controls |
| Alt+V / Alt+A | Video/audio source; Windows F2 opens the file picker |
| Alt+R / Alt+S / Alt+O | Record / finish recording / open recordings |
| Alt+J | Join voice by guild/channel ID |
| F1 / Esc / Ctrl+Q | Help / cancel form / quit |

Windows retains native console input, mouse and Unicode clipboard paste.
Linux/macOS decode xterm arrows/function keys, Alt, UTF-8, SGR mouse and bracketed
paste. Use the terminal's paste shortcut; pasted newlines remain text.
Ctrl+number and Ctrl+Enter need an extended keyboard protocol in POSIX terminals;
the F-key views and friend/profile actions remain available through the palette.
Use at least 100x30 cells when possible. Narrow layouts hide the right column.

## Media builds and local files

The default portable CI packages build the REST/Gateway client. Full voice,
camera/Go Live, receive/playback and recording need `NOVA_NATIVE_MEDIA=ON`,
libdave, MLSPP, OpenSSL, Opus and FFmpeg. See [native media setup](https://github.com/Cavanshirpro/discord.cpp-self/blob/main/docs/MEDIA_NATIVE.md).
Prepare those dependencies, add their prefix to `CMAKE_PREFIX_PATH`, then enable
`-NativeMedia` in PowerShell, `--native-media` in the shell wrapper or
`-DNOVA_NATIVE_MEDIA=ON` in CMake. A build without media reports that explicitly
when joining; it does not substitute simulated voice in an account session.
The maintainer's adopted Windows build enables both native media and browser TLS.

The TUI persists drafts to `drafts.json` beside its configuration. Settings and
last selections use `config.json`; recordings/cache are also local. These paths
are ignored by Git. The optional event log records operation/status/error codes,
not tokens or message text. The GUI continues to keep its own drafts in memory.

Tests cover the imported editor/model/layout, real backend jobs against local
HTTP/HTTPS fixtures, fragmented terminal input and POSIX PTY restoration. They
use fake tokens and never log into Discord.

## Latest validated Windows build

The PowerShell build wrapper runs tests before promoting a TUI. Promotion copies
the executable, adjacent DLLs and CA bundle into an immutable directory under
`build/live-tui`, then replaces `build/tui-current.cmd`. Native-media builds also
update `build/tui-current-native.cmd`; the launcher prefers this full build.
An open session can keep using its current files while the next build finishes.
`-SkipTests` does not promote an unverified version. To promote a custom build
after running its full CTest suite, run:

```powershell
./scripts/publish-tui.ps1 -BuildDirectory ./build/adopted-native-mingw64
```

The UI owns up to 12 channel histories with 500 messages each; the SDK's duplicate
message cache is disabled. Wrapped chat lines are reused until messages, channel,
search or width changes. Idle loops check input at most every 50 ms and do not
redraw unchanged frames. A channel history error remains visible until a successful
refresh; HTTP 403 means the account cannot read that channel's history.
