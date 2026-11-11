---
name: bump-pin
description: Move one submodule (here examples/esp32/scripts) to a release tag in its own PR. Use when a pinned repository released and this repository should follow.
---

# Bump a pin

This repository pins one submodule: `examples/esp32/scripts` (kokeb-labs/espidf-tools), used only to build
the ESP32 examples. Rules: `standards/release.md#pins-and-the-manifest` and the org CONTRIBUTING.md.

1. Branch `chore/bump-espidf-tools-vX.Y.Z` from an up-to-date `main`.
2. `git -C examples/esp32/scripts fetch --tags && git -C examples/esp32/scripts checkout vX.Y.Z`; pin to a tag,
   never a branch. If the repository has no tag yet, pin the commit of its latest green `main` and say so.
3. `git add examples/esp32/scripts`. Change nothing else in the same PR.
4. If the scripts changed `app_config.yml` keys or build flags, read their changelog and list the changes in the PR.
5. Run the `verify` skill (the ESP32 build itself runs in CI).
6. Open the PR titled `Bump espidf-tools to vX.Y.Z`, linking the release, and wait for
   `CI gate / gate`. Never point the submodule at an unmerged branch.
