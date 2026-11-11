## Summary

<!-- What changes, in one or two sentences. -->

## Why

<!-- The problem this solves. Link the issue, the datasheet section or the upstream PR. -->

## How verified

- [ ] CI green, including `CI gate / gate`
- [ ] Ran locally before pushing (`.claude/skills/verify`): <!-- host tests, doc snippets, lint, docs build -->

## Bench status

<!-- One of the two. Without a bench run, give the steps a person with the hardware follows. -->

- [ ] **Verified on hardware**:
  <!-- board / MCU / port, chip revision, what was run, what was observed -->
- [ ] **Not verified on hardware**. Bench steps for a human:
  1. <!-- flash examples/esp32 app ..., expect ... -->

## Standard

- [ ] Follows the [Kokeb Labs engineering standard](https://github.com/kokeb-labs/.github/tree/main/standards),
  or each deviation is listed here with the rule and its issue

## Checklist

- [ ] PR title is an imperative commit subject (it becomes the squash commit)
- [ ] One logical change; no unrelated edits
- [ ] README / `docs/` and Doxygen updated for any behaviour or API change
- [ ] `CHANGELOG.md` entry under `## [Unreleased]`; breaking changes under `### Breaking`
- [ ] Tests added or updated, including failure paths (call before `EnsureInitialized()`, failing port hook)
- [ ] Submodule pins bumped where needed, linking the PR or release pulled in
- [ ] No secrets, credentials or private datasheet material; no attribution lines
