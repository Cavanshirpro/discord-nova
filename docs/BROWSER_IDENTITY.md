# Browser and TLS identity

Development builds now offer **Chrome 152** and **Default TLS** connection
profiles in the GUI and TUI. Chrome is selected by default when the build has
browser support. Choose the profile before connecting; an active session keeps
its selected profile. Demo mode makes no network connection.

This uses [curl-impersonate v2.3.2](https://github.com/lexiforest/curl-impersonate/releases/tag/v2.3.2),
not just a User-Agent string. The provider configures TLS ClientHello, browser
headers, HTTP/2 SETTINGS and flow control. Nova sends requests through
discord.cpp-self; there is no second Discord transport. REST keeps the profile's
HTTP/2 negotiation, and the Gateway uses the provider's WebSocket profile with
HTTP/1.1 Upgrade.

This is protocol emulation, not a running browser: there is no JavaScript engine,
browser cookie import or generated Discord build/device identity. It does not
guarantee identical behavior in every browser scenario. Certificate verification,
hostname validation, rate limits and verification challenges remain enforced.

## Build and select

`NOVA_BROWSER_IDENTITY=ON` is the default for development builds. CMake downloads
the pinned shared-library SDK for Windows x64 (all four compilers), Linux
x64/ARM64 or macOS x64/ARM64 and verifies a platform-specific SHA-256. SDK
licenses are installed with the runtime. Published v0.1.0 binaries predate this
feature; rebuild this revision to use it.

```powershell
./scripts/build.ps1 -Toolchain mingw64
./build/mingw64/discord-nova-tui.exe --browser-profile chrome152
```

```sh
sh scripts/build.sh
./build/linux/discord-nova-tui --browser-profile chrome152
```

The GUI has a **Connection profile** selector. The TUI has a **Next connection**
selector; its header reports the profile of the connected session. Advanced
process environment overrides are available for both frontends:

- `NOVA_BROWSER_PROFILE=chrome152` selects a provider profile. `default` disables
  impersonation. Other target names must exist in this pinned provider release.
- `NOVA_CA_BUNDLE=/absolute/path/roots.pem` selects an explicit trusted PEM bundle.

The app otherwise uses `cacert.pem` beside the running executable, or in
`Contents/Resources` inside a macOS GUI app bundle. The distributed bundle is the pinned Mozilla export
dated 2026-10-20. TLS certificate and hostname checks stay enabled. Never disable
them to make a connection work.

For a system-curl build use a fresh build directory and configure
`-DNOVA_BROWSER_IDENTITY=OFF`, or pass `-SystemCurl` to PowerShell /
`--system-curl` to the shell wrapper. Such a build does not support browser
profiles and rejects a requested profile explicitly. Default TLS uses the linked
library's ordinary TLS settings; it is not a second dynamically selected backend.

## Tests

`browser_tls` captures a ClientHello on a local TLS socket and checks the Chrome
cipher order, GREASE, certificate-compression advertisement and ALPN. Its minimal
HTTP/2 server verifies SETTINGS order/values, connection-level flow control and
connection reuse. Separate negative cases reject an unrelated CA and a wrong
hostname before any authenticated HTTP is sent.

`browser_loopback` exercises account/servers/history/send/error/shutdown over
local HTTPS and WSS, checking the selected User-Agent on both transports. The
fixtures use public disposable test keys and fake tokens. No operating-system
trust store is modified and no real Discord login is performed.
