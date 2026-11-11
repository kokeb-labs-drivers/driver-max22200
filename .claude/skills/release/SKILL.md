---
name: release
description: Prepare and tag a release candidate, then the final release, following the org release checklist. Use when asked to release or tag this repository.
---

# Release

The rule is `standards/release.md`; the per-repository list is `standards/checklists/release.md`. Agents
prepare; a maintainer pushes tags unless the owner said otherwise in this session.

## 1. Before

1. `main` is green, including `CI gate / gate`; no open issue labelled `safety` affects this version. For a new
   repository the first green `main` comes before any tag: the docs publish to `gh-pages` from `main` first, so a
   release candidate is never the first publish (section 5 says why a tag cannot be re-run to pick up a fix).
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

## 5. When CI misbehaves

Tell a failure of the infrastructure from a failure of the code before re-running anything.

- **Infrastructure** (re-run the failed jobs, at most twice): the failing step is an install, a download, an
  upload or a job set-up (`apt-get`, a mirror, `Install ...`, `Upload ...`); a job sat queued with no runner and
  the run was then marked completed (GitHub calls it *abandoned*, and the CI gate and any record job that needed it
  fail after it); the API answers 502; an artifact upload says the storage quota is full (a private repository on the
  free plan shares a small quota; a public one does not count against it, and old artifacts can be deleted).
  Check `githubstatus.com`, re-run with `gh run rerun <id> --failed`, and say in the PR that it was infrastructure.
- **Real**: anything else, including a step of ours that fails the same way twice. Read the log, fix the cause and
  push; never re-run a real failure until it turns green.
- **A re-run replays the same workflow versions.** `gh run rerun` uses the `@v1` commit the run started with, so a
  fix released in `kokeb-labs/ci-tools` since then is not picked up. The "Uses:" line in a job's log names the
  commit. To pick the fix up, push again: a new commit on the branch, or a new tag. Releasing the fix in ci-tools
  comes first, and the driver's own files stay as they are.
- **Tags are not moved or deleted to retry.** The `release-tags` ruleset forbids it on purpose. Cut the next
  `-rc.N`, unless the owner says in this session to delete a tag that published nothing.
- **Local scripts run under `bash`**, not `zsh`: an unquoted variable holding a list is not split into words in
  `zsh`, and a polling loop over it queries one wrong name for as long as it runs.
