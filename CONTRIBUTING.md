# Contributing

Open an issue for larger changes so we can agree on scope. Keep shared behavior
in `nova_core`, frontend concerns in their GUI/TUI files and Discord transport
in discord.cpp-self. Do not introduce a second Discord networking library.

Build the enabled interfaces and run `ctest --test-dir build/<preset>
--output-on-failure`. New protocol behavior needs a local fixture; never make CI
depend on a real account or token. Preserve tokens/drafts, bounded memory and
worker shutdown behavior. Include platform, toolchain and redacted steps when
reporting bugs.

Plugin support is deferred. Changes must not imply a stable plugin API in v0.1.
Library defects should be reduced and fixed in discord.cpp-self before bumping
the dependency pin. See `docs/LIBRARY.md`.

Contributions are licensed under GPL-3.0-or-later. Do not commit credentials,
build trees, account screenshots, downloaded dependency trees or binary releases.
