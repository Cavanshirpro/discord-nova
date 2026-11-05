# Building discord-nova

All commands run from the repository root. Do not mix Qt, curl or C++ runtimes
from different Windows toolchains. Every preset has its own build directory.

Development builds enable `NOVA_BROWSER_IDENTITY=ON` by default. CMake downloads
the pinned, SHA-256-verified curl-impersonate SDK and Mozilla CA bundle for the
selected platform. See [browser identity](BROWSER_IDENTITY.md) for profiles and
trust configuration. The published v0.1.0 archives predate this feature.

## Windows: MSYS2

Install [MSYS2](https://www.msys2.org/) and update it. In the matching terminal:

```sh
# MINGW64
pacman -S --needed git mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-qt6-base mingw-w64-x86_64-curl mingw-w64-x86_64-nlohmann-json mingw-w64-x86_64-zlib mingw-w64-x86_64-python
sh scripts/build.sh
```

```sh
# UCRT64
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-nlohmann-json mingw-w64-ucrt-x86_64-zlib mingw-w64-ucrt-x86_64-python
sh scripts/build.sh
```

```sh
# CLANG64
pacman -S --needed git mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-qt6-base mingw-w64-clang-x86_64-curl mingw-w64-clang-x86_64-nlohmann-json mingw-w64-clang-x86_64-zlib mingw-w64-clang-x86_64-python
sh scripts/build.sh
```

From PowerShell or CMD, after installing those packages:

```powershell
./scripts/build.ps1 -Toolchain mingw64
./scripts/build.ps1 -Toolchain ucrt64
./scripts/build.ps1 -Toolchain clang64
./scripts/build.ps1 -Toolchain mingw64 -MsysRoot D:/msys64 -Jobs 4
# Source executables need the matching toolchain bin on PATH.
$env:PATH = 'C:/msys64/mingw64/bin;' + $env:PATH
./build/mingw64/discord-nova-gui.exe --demo
./build/mingw64/discord-nova-tui.exe --demo
```

```bat
scripts\build.cmd -Toolchain mingw64
scripts\build.cmd -Toolchain ucrt64 -TuiOnly
scripts\build.cmd -Toolchain clang64 -Package
```

Git Bash can use `scripts/build.cmd` or call the PowerShell wrapper. Use a native
MSYS2 toolchain environment for `build.sh`; Git Bash alone does not supply Qt or GCC.

## Windows: MSVC

Install Visual Studio 2022 C++ build tools, CMake, Ninja, Python, Git and **Qt 6.8.3
MSVC 2022 x64** (`qtbase` is enough). Open an x64 Developer PowerShell/Command
Prompt. Add Qt's `bin` to PATH and set `CMAKE_PREFIX_PATH` to its installation.
Bootstrap [vcpkg](https://github.com/microsoft/vcpkg) at the baseline in `vcpkg.json`:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:/dev/vcpkg
git -C C:/dev/vcpkg checkout 9a5dfef5d1a0438a7d24856a85bd60f309d355e9
& C:/dev/vcpkg/bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = 'C:/dev/vcpkg'
$env:CMAKE_PREFIX_PATH = 'C:/Qt/6.8.3/msvc2022_64'
$env:PATH = "$env:CMAKE_PREFIX_PATH/bin;$env:PATH"
./scripts/build.ps1 -Toolchain msvc -Package
```

The manifest installs curl with the `websockets` feature, nlohmann/json and zlib.
Browser builds link the pinned SDK instead of that system curl.
The GUI uses the prebuilt Qt installation. In CMD, use `set VAR=value` and
`scripts\build.cmd -Toolchain msvc`. The same preset works from the VS Code
developer terminal. MSYS2's CMake/Ninja must not take precedence in this shell.

## Linux

Ubuntu 24.04 x64/ARM64:

```sh
sudo apt-get update
sudo apt-get install -y build-essential git cmake ninja-build python3 qt6-base-dev libgl1-mesa-dev libssl-dev zlib1g-dev nlohmann-json3-dev
cmake --preset linux '-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib'
sh scripts/build.sh --preset linux --jobs 4
./build/linux/discord-nova-gui --demo
./build/linux/discord-nova-tui --demo
python3 scripts/package.py --build-dir build/linux --platform linux-x64
```

For a system-curl build, pass `--system-curl` to `build.sh` or
`-DNOVA_BROWSER_IDENTITY=OFF` to CMake. If system curl omits ws/wss,
`setup-curl.py` builds pinned curl 8.17.0; configure with
`-DCMAKE_PREFIX_PATH="$PWD/build/tools/curl"`. Reconfiguring via `build.sh`
preserves the prefix and RPATH in the CMake cache.
Other distributions need equivalent packages and a C++20 compiler. Wayland/X11
and font support come from your Qt runtime packages. Published Linux binaries
use Ubuntu 24.04 system Qt; the selected curl runtime is included in `lib/`.

## macOS

Use macOS 15+, **Xcode 26 or newer** (its libc++ supports `std::jthread`) and
[Homebrew](https://brew.sh/). Xcode 16's default libc++ does not enable that
library feature. Select the newer toolchain before configuring:

```sh
brew install cmake ninja qtbase curl nlohmann-json python dylibbundler
export DEVELOPER_DIR=/Applications/Xcode_26.3.app/Contents/Developer
export PATH="$(brew --prefix qtbase)/bin:$PATH"
cmake --preset macos -DCMAKE_PREFIX_PATH="$(brew --prefix qtbase);$(brew --prefix curl);$(brew --prefix nlohmann-json)"
sh scripts/build.sh --preset macos --jobs 4
open build/macos/discord-nova-gui.app --args --demo
./build/macos/discord-nova-tui --demo
python3 scripts/package.py --build-dir build/macos --platform macos-arm64
```

Use `macos-x64` on Intel. `macdeployqt` deploys the app bundle; `dylibbundler`
relocates terminal-client libraries. Packaging ad-hoc signs both after relocation.
No developer certificate or notarization secret is needed. Adjust the Xcode app
path to your installed version; CMake checks `std::jthread` at configuration.

## Select an interface / local library

```sh
# These wrappers also work when invoked from fish or zsh.
sh scripts/build.sh --tui-only
sh scripts/build.sh --gui-only
# CMake is the underlying interface for every shell.
cmake --preset default -DNOVA_BUILD_GUI=OFF
cmake --build --preset default --parallel 4
ctest --preset default
```

PowerShell equivalents: `-TuiOnly`, `-GuiOnly`, `-SkipTests`, `-Jobs 4`, `-Package`.
Use `-SystemCurl` to disable browser profiles in the PowerShell/CMD wrapper.
Use `-NativeMedia` / `--native-media` with prepared libdave/MLSPP/OpenSSL/Opus
dependencies to enable the original TUI's voice/video backend. Pass the dependency
prefix through CMake; see [TUI media builds](TUI.md). The default is OFF.
A TUI-only build does not require Qt. Both interfaces still use discord.cpp-self.
The Qt GUI theme lives in `styles/nova.qss` and is embedded by `qt_add_resources`.
Edit QSS and rebuild the GUI to change the style; binary packages need no separate
stylesheet path. Layout and application behavior remain in the C++ widgets.
To develop against a local library checkout:

```sh
cmake --preset default -DNOVA_DISCORD_SOURCE=/absolute/path/to/discord.cpp-self
```

Do not publish a release built against uncommitted local dependency changes.
Release CI downloads the exact reviewed library commit. If a library change is
needed, report/reproduce and commit it there, then update the reviewed pin here.
Published releases must document the precise dependency commit.

## Test and release

`ctest` runs core tests, local REST/Gateway integration tests and demo startup
checks for enabled frontends. Nothing logs into a real account.
The original TUI adds editor/model/layout tests and backend HTTP/HTTPS fixtures;
POSIX builds also exercise an actual PTY, UTF-8 paste and terminal restoration.
Browser builds also check Chrome ClientHello, HTTP/2 settings, connection reuse,
HTTPS/WSS integration and rejection of untrusted certificates/wrong hostnames.
`package.py` installs the Runtime component into a fresh staging directory, deploys runtimes,
checks staged demo startup, includes notices, then creates an archive and SHA-256.
An existing staging directory is rejected; choose a fresh label or clean it
manually after reviewing its path.

To release, update version metadata and notes, push the reviewed commit to main,
create an annotated `vX.Y.Z` tag, and push it. The release workflow builds every
platform again from that tag. All eight platform builds and source packaging
must pass before publication. Manual dispatch takes an existing tag; it refuses
to overwrite an already published release. Download checksums are in `SHA256SUMS`.
