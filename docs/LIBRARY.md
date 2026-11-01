# discord.cpp-self integration

v0.1.0 pins discord.cpp-self v0.2.1, commit
`a8681265e0c6ee58017639070c891d06d9624d8e`. CMake links `discord::self`.
No library fork or replacement network implementation is included in Nova.

The shared service serializes REST calls on one worker. A second Client drives
Gateway callbacks; only bounded view data crosses into the frontend. History
loads defer incoming creates/patches/deletions and replay them after the REST
snapshot, preventing an older response from overwriting live changes.

Existing DMs use `Client::request("GET", "/users/@me/channels")` because the
library has no typed list-DMs convenience method. JSON is parsed into the
library's Channel model; history and sends still use typed library methods.

## Observed integration behavior

- The v0.2.1 library target needs `NOMINMAX` when built with MSVC: Windows SDK
  macros otherwise expand `std::min/max` and `numeric_limits::max` in its curl
  transport sources. Nova adds private compile definitions to that target,
  without changing library source files. This should also be fixed in the
  library's own Windows target configuration.
- `Client::run()` resets its stop flag during startup. Nova repeats `close()`
  until the Gateway thread exits before joining, covering immediate disconnect.
  The offline integration test exercises this sequence. No library files changed.
- Gateway needs curl with WebSocket support. The Linux workflow builds a pinned
  curl; MSVC uses vcpkg's `websockets` feature; MSYS2/Homebrew provide it.
- HTTP calls are synchronous. They run outside the UI thread. Shutdown waits for
  an in-flight REST call to finish or time out; UI actions do not cancel a send.
- Native media remains disabled in this release. Media work should be implemented
  against the library's media API in a future release.

The MSVC build configuration issue is handled in Nova's CMake integration. Real Discord login,
account permissions, verification and media are not validated by these fixtures.
Report new library failures to the maintainer with a minimal repro, dependency
commit, platform and redacted diagnostics. Never include an account token.
