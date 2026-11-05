# Third-party software

Nova's own code is GPL-3.0-or-later. Dependencies retain their respective licenses.
Distributed runtime notices are included under `share/licenses/` in binary archives.

| Dependency | Source / version | License |
| --- | --- | --- |
| curl-impersonate (development browser builds) | [v2.3.2](https://github.com/lexiforest/curl-impersonate/releases/tag/v2.3.2), platform archives verified by SHA-256 | MIT/curl; bundled BoringSSL, Brotli, c-ares, nghttp2/3, ngtcp2, zlib and zstd retain SDK license notices |
| Mozilla CA bundle (browser builds) | [2026-10-20 export](https://curl.se/ca/cacert-2026-10-20.pem) | MPL-2.0 |
| discord.cpp-self | [v0.2.2 + browser identity API](https://github.com/Cavanshirpro/discord.cpp-self/tree/2e5659faf5231aa00d16850e64b35aaa9ec5d8c3) | LGPL-3.0-or-later |
| Qt Core / GUI / Widgets | [Qt source](https://code.qt.io/cgit/qt/qtbase.git/), system packages or MSVC 6.8.3 | LGPL-3.0 / GPL-3.0; individual files may use other compatible terms |
| curl | [curl source](https://github.com/curl/curl), Linux 8.17.0; other platforms' package versions | curl license |
| nlohmann/json | [source](https://github.com/nlohmann/json), 3.11+ | MIT |
| zlib | [source](https://github.com/madler/zlib) | Zlib |
| OpenSSL (where used by curl) | [source](https://github.com/openssl/openssl), package-manager version | Apache-2.0 |

Windows MSVC dependencies use the vcpkg baseline in `vcpkg.json`; MSYS2 and
Homebrew dependencies follow their package repositories at build time. Those
package managers may bring additional runtime dependencies. Package-provided
copyright files are copied into release archives. Build logs record resolved
versions. Qt is dynamically linked. The app does not use Qt WebEngine or QML.

GPL/LGPL license texts are included with the source and dependency notices.
The complete Nova source for each binary release is the matching release tag
and source archive; discord.cpp-self is pinned above. The original TUI uses its
own C++ terminal renderer. Historical v0.1.0 archives include FTXUI v7.0.3 and its
MIT notice; development builds no longer link it. Optional native-media builds
also use libdave (MIT), MLSPP (BSD-2-Clause), Opus (BSD-style), OpenSSL and FFmpeg
(license depends on the installed build); follow the library's native-media
distribution instructions and include their notices when distributing those builds.
To rebuild,
follow `docs/BUILDING.md`. Preserve these notices when redistributing binaries.
