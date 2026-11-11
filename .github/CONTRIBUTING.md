# Contributing

This repository follows the [Kokeb Labs engineering standard](https://github.com/kokeb-labs/.github/tree/main/standards)
and the organisation's [CONTRIBUTING.md](https://github.com/kokeb-labs/.github/blob/main/CONTRIBUTING.md). This page is
the short version for this repository.

## Branches

`main` is the only long-lived branch; changes reach it through squash-merged pull requests. Branch from `main`:

| Prefix | For |
| --- | --- |
| `feat/<topic>` | New behaviour or API |
| `fix/<topic>` | A bug fix |
| `docs/<topic>` | Documentation only |
| `ci/<topic>` | Workflows, lint and build configuration |
| `chore/<topic>` | Pin bumps, version bumps, renames, housekeeping |
| `release/x.y` | A maintenance line for an older minor, created from its tag by a maintainer |

## Pull requests

- **Squash merge only.** The PR title becomes the commit subject on `main`: imperative, under about 72
  characters, no trailing period (`Add FIFO read`).
- **One logical change**, ideally under about 400 lines of diff.
- Fill in the PR template, including **Bench status**: the board you ran it on, or "not verified on hardware"
  with the bench steps.
- Docs, Doxygen and `CHANGELOG.md` (`## [Unreleased]`, `### Breaking` when callers must change) in the same PR.
- Sign off every commit (`git commit -s`).
- No attribution lines in commits, PR descriptions or code: no `Co-Authored-By:` trailers for tools, no
  "Generated with" footers.

## Before you push

```bash
cmake -S . -B build -DMAX22200_BUILD_TESTS=ON
cmake --build build --parallel && ctest --test-dir build --output-on-failure
kokeb-docs check && mkdocs build -f _config/mkdocs.yml --strict -d /tmp/site
```

The [`verify` skill](../.claude/skills/verify/SKILL.md) lists every check CI runs.

## Definition of done

- [ ] It does one thing, and the PR says what, why and how it was verified.
- [ ] It follows the standard, or the PR names each deviation and links its issue.
- [ ] Every CI check passes locally: host tests, doc snippets, `clang-format` and `clang-tidy` on changed files,
      markdownlint, `kokeb-docs check`, `mkdocs build -f _config/mkdocs.yml --strict`.
- [ ] New behaviour has host tests, including the failure paths (a failing port hook, a call before
      `EnsureInitialized()`).
- [ ] The README or `docs/` page and the Doxygen comments describe the change.
- [ ] `CHANGELOG.md` has an entry under `## [Unreleased]`.
- [ ] CI is green, including `CI gate / gate`.
- [ ] Anything that needs hardware is marked **not verified on hardware**, with bench steps.

## Security

Report vulnerabilities privately ([SECURITY.md](SECURITY.md)), not in issues.
