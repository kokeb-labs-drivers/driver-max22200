---
name: publish-driver
description: Publish a finished driver in the owner's order - iterate privately, keep a local copy, publish a fresh single-commit copy into the public repository, let CI run, enable Pages, then tag the release candidate v0.1.0-rc.N. Lists the check before each step, the history and secret scan, and what only the owner may do. Use after first-push passed and the owner asked to publish.
---

# Publish a driver

The order is fixed. Each step has a check that must pass before the next one starts; a step that fails is fixed
and repeated, never skipped.

1. Iterate privately (private repository or local only) until `first-push` passes.
2. Keep a local copy of that state.
3. Publish a **fresh, clean, single-commit copy** into the public repository.
4. Let CI run on `main`, and read it.
5. Enable Pages.
6. Tag `v0.1.0-rc.N`. Release candidates come after the repository is public, never before.

Nothing public happens on an agent's own initiative. Section 1 says who does what.

## 1. Who may do what

| Action | Agent | Owner only |
| --- | --- | --- |
| Local backup, fresh commit, scans, a push of that one commit to an **empty** repository the owner made | yes, when the owner asked to publish in this session | |
| Make a repository public, or create the public repository | | yes (`gh repo edit ... --visibility public`) |
| Replace the history of an existing repository (delete and recreate, or force-push) | | yes, with the backup named and open |
| Rulesets, merge options, security features, Pages, `kokeb_org.py --apply` | commands prepared and run only on the owner's yes in this session | otherwise yes |
| Tag `vX.Y.Z-rc.N` | yes, when the owner said so in this session | otherwise yes |
| Tag the final `vX.Y.Z`, mark a release as latest, publish to a package registry | | yes, always |
| Any `hardware-verified` claim (level H2) | | the hardware engineer, then the owner |

Stop and report when a check fails twice for the same reason, when the repository already has history you were not
told about, or when an action is in the owner-only column and no yes was given.

## 2. Before anything: the gate

Everything in `first-push` has passed for the tree to be published, and the independent review stopped at
`NO BLOCKER`. Note the tree id of that state: `git rev-parse HEAD^{tree}`; the fresh commit must have the same one.

## 3. Keep a local copy

```bash
mkdir -p ../backups && git bundle create ../backups/driver-<chip>-prepublish.bundle --all
git bundle verify ../backups/driver-<chip>-prepublish.bundle
cp -a . ../backups/driver-<chip>-prepublish-tree    # the working tree too, with submodule folders
```

Keep both outside the repository and outside every folder that is cleaned automatically. The bundle verifies, or
nothing else happens. Review notes, build logs and any file that is not in the repository go into the backup, not
into the fresh commit.

## 4. Make the fresh single commit

An orphan branch keeps the index and so the whole tree, including the ESP-IDF scripts submodule as a gitlink, and
leaves the history behind:

```bash
git switch --detach HEAD && git checkout --orphan public-main
git add -A && git status --short | head           # exactly the reviewed tree
git -c user.name="<owner name>" -c user.email="<id>+<user>@users.noreply.github.com" \
    commit -s -m "<CHIP> driver, first version"
```

Use the owner's noreply identity (the one in the repository's other commits or the owner's instruction, never a
personal address). The message has the subject, a short body saying what the driver covers and its hardware level
(`hardware-validation` skill), and the sign-off. It has no `Co-Authored-By:`, no "Generated with" line, no tool name.

## 5. The history and secret scan (on `public-main`)

All of these pass, and the output goes into the PR-less record: the first commit's body or the owner report.

```bash
test "$(git rev-list --count public-main)" = 1                       # one commit
test "$(git rev-parse public-main^{tree})" = "<tree id from section 2>"   # the reviewed tree
git log -1 --format='%an <%ae>%n%cn <%ce>' public-main                # noreply identity only
git log -1 --format=%B public-main | grep -c '^Signed-off-by: '      # 1
git log -1 --format=%B public-main | grep -iE 'co-authored-by|generated with|claude code|copilot' && echo FOUND   # no output
git ls-tree -r public-main --name-only | grep -iE '\.(pdf|p12|pfx|pem|key|env)$|id_rsa|secret|token'      # no output
git ls-tree -r -l public-main | awk '$4 > 1000000 {print $4, $5}'    # nothing over 1 MB without a reason
python3 tools/check_standalone.py --strict
git grep -nIE '(AKIA[0-9A-Z]{16}|ghp_[A-Za-z0-9]{36}|github_pat_|-----BEGIN [A-Z ]*PRIVATE KEY|xox[abp]-|Bearer [A-Za-z0-9._-]{20,})' public-main
gitleaks detect --no-banner --redact --source . --log-opts public-main      # when installed; say so if not
```

Also read `.gitmodules` (only `kokeb-labs/espidf-tools` over https), `.github/CODEOWNERS`, `driver.yml` and
`docs/datasheets.yml` (URLs and hashes, no local path). A hit is fixed in the working tree, the commit is made
again, and the whole section repeats. A secret that was ever committed in the private history is not a problem for
the fresh commit but is a problem for the owner: report it so the credential is rotated.

## 6. Push (the repository is empty and public)

The owner created the public repository, empty (no README, no licence, no template files). Check it:

```bash
gh repo view kokeb-labs-drivers/driver-<chip> --json visibility,isEmpty,defaultBranchRef
git remote add public https://github.com/kokeb-labs-drivers/driver-<chip>.git
git push public public-main:main          # a plain push to an empty branch; never --force
```

If the repository is not empty, stop: replacing history is owner-only (section 1). At most three repositories have
CI running at once; wait for a free slot.

## 7. Let CI run

`gh run list -R kokeb-labs-drivers/driver-<chip> --branch main` until every workflow of the push completed. Read
each red job before any re-run (`release` skill, section 5: only infrastructure failures are re-run, at most twice).
A real failure is fixed in the private tree, which then goes through `verify`, a new fresh commit and this section
again; the public `main` changes only by ordinary pull requests once it exists, so for the first publication prefer
fixing and republishing to an empty repository the owner recreates over force-pushing. Add the cause to
`docs/recurring-defects.md` and a lint or test when it is a new class (`docs/mechanization-policy.md`).
The result needed: `CI` green on `main`, including `CI gate / gate`.

## 8. Settings and Pages

On the owner's yes (or prepared for the owner): `python3 org/kokeb_org.py --org kokeb-labs-drivers --repo
driver-<chip> --apply` from a checkout of `kokeb-labs/.github` (rulesets, secret scanning and push protection,
CodeQL, vulnerability reporting, labels including `needs-hardware` and `hardware-verified`; run it without
`--apply` first and read the plan; a 422 on the last call: run again after a few seconds). Add the `CI gate / gate`
check to the `main-review` ruleset. Then enable Pages from the `gh-pages` branch, root: the docs workflow created
that branch on the first green `main`.

```bash
gh api -X POST repos/kokeb-labs-drivers/driver-<chip>/pages -f 'source[branch]=gh-pages' -f 'source[path]=/'
curl -fsSI https://kokeb-labs-drivers.github.io/driver-<chip>/ | head -1       # 200
python3 org/kokeb_org.py --org kokeb-labs-drivers --repo driver-<chip> --check  # 0 change(s) pending
```

The site opens and its links work (the `ci-docs-linkcheck` workflow is green on a new push or run).

## 9. Tag the release candidate

Check first: `main` is green after the settings, `CHANGELOG.md` has its first release section ready for the `release`
skill (sections 1 and 2), `driver.yml` `hardware.level` and the README badge agree (`tools/check_hardware.py`), and
the release body has the wording of the `hardware-validation` skill for the level.

```bash
git switch main && git pull --ff-only
git tag -a v0.1.0-rc.1 -m "v0.1.0-rc.1" && git push public v0.1.0-rc.1
```

Tags are never moved or deleted; a fix is `-rc.2` (`release` skill). The Release workflow publishes the pre-release
with firmware, SBOM and checksums: download them and check `sha256sum -c SHA256SUMS`. The final `vX.Y.Z` is the
owner's, on the same commit as the last candidate, after the hardware level the owner wants for it.

## 10. Report

Give the owner: the repository URL, the commit id and tree id, the scan output, the CI run ids, the Pages URL, the
tag, the hardware level and what is not verified, and the location of the local backup. Then the pin-bump work for
consumers (`bump-pin` skill) is a separate pull request in each consumer.
