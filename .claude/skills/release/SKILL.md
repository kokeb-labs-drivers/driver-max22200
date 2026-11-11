---
name: release
description: Prepare and tag a release candidate, then the final release, following the org release checklist. Use when asked to release or tag this repository.
---

# Release

The rule is `standards/release.md`; the per-repository list is `standards/checklists/release.md`. Agents
prepare; a maintainer pushes tags unless the owner said otherwise in this session.

## 1. Before

1. `main` is green, including `CI gate / gate`; no open issue labelled `safety` affects this version.
2. Choose the version: a breaking change bumps the minor (`0.1.x` to `0.2.0`), anything else the patch. Use
   `-rc.N` while the release is benched. A driver no board uses MAY go straight to the final release (CI-only).
3. Read `## [Unreleased]` in `CHANGELOG.md`: every breaking change is under `### Breaking` with what callers
   change; headings are exactly `### Breaking`, `### Added`, `### Changed`, `### Deprecated`, `### Removed`,
   `### Fixed`, `### Security`, in that order.

## 2. Prepare (branch `chore/release-vX.Y.Z`)

1. Set `MAX22200_VERSION_MAJOR/MINOR/PATCH` in `cmake/max22200_build_settings.cmake`.
2. Set `PROJECT_NUMBER` in `_config/Doxyfile` to the same `X.Y.Z` (without `-rc.N`); the CTest
   `max22200_version_copy_*` test fails until it matches. (There is no `idf_component.yml`: ESP-IDF projects use
   the repository root as a component, and the version comes from the git tag.)
3. Move `## [Unreleased]` to `## [X.Y.Z] - YYYY-MM-DD` and leave an empty `## [Unreleased]` above it.
4. Run the `verify` skill; the install-consumer test proves `find_package(max22200)` and `max22200::max22200`.
5. Open the PR titled `Release vX.Y.Z` with the release checklist pasted in. It is squash-merged.

## 3. Release candidate

1. Tag the merge commit on `main`: `git tag -a vX.Y.Z-rc.1 -m "vX.Y.Z-rc.1"` and push the tag (maintainer).
2. `release.yml` creates the GitHub Release; set its body to the changelog section and state the bench status:
   the board and test list, or **CI-only**.
3. Bench it (a human): flash an MCU example from `examples/`, run the bench steps from the merged PRs.

## 4. Final

1. If the rc passed, tag **the same commit** `vX.Y.Z`. If anything changed, cut `-rc.N+1` instead.
2. Check the docs published for the tag and `latest` moved (an rc does not move `latest`).
3. Open the pin-bump PRs in the repositories that consume this one, each linking the release (`bump-pin` skill there).
