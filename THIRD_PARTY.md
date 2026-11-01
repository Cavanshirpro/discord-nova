# Third-party software

Nova's own code is GPL-3.0-or-later. Dependencies retain their respective licenses.
Distributed runtime notices are included under `share/licenses/` in binary archives.

| Dependency | Source / version | License |
| --- | --- | --- |
| discord.cpp-self | [v0.2.1](https://github.com/Cavanshirpro/discord.cpp-self/tree/a8681265e0c6ee58017639070c891d06d9624d8e) | LGPL-3.0-or-later |
| FTXUI | [v7.0.3](https://github.com/ArthurSonzogni/FTXUI/tree/f921fad208912747c17d129a8ef75ec7624b6eec) | MIT |
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
and source archive; discord.cpp-self and FTXUI are pinned above. To rebuild,
follow `docs/BUILDING.md`. Preserve these notices when redistributing binaries.
