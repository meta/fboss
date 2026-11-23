# CI, and how source reaches this repository

## This repository is a one-way export

`github.com/meta/fboss` is generated from an internal source of truth.
Code lands internally first and is exported automatically, continuously.

Consequences that matter in practice:

- **Pull requests are not merged on GitHub.** A PR is imported as an internal
  change, reviewed and landed there; the export then closes the PR as merged.
  Expect your commit to appear under a bot author with a different hash.
- Commit messages are rewritten on export. Only a few sections survive, so
  anything you want visible publicly belongs in the summary.
- Some files are deliberately not exported. If a code comment or document
  references a path that does not exist here, it is internal-only.
- You may see markers such as `// @oss-disable:` and `// @oss-enable` in the
  source. These are how lines are conditionally enabled per environment.
  **Do not "clean them up"**; they are rewritten in both directions and edits
  will not round-trip.

## The public CI

Workflows live in `.github/workflows/`. Do not rely on a written list; read
the directory, because it changes. The shape is consistent:

1. A build job on a large runner invokes
   `fboss/oss/scripts/docker-build.py --target <cmake target>`.
2. A packaging step runs `package-fboss.py` to produce `fboss_bins.tar.zst`,
   and the image is saved as an artifact.
3. A dependent test job loads the image and runs
   `fboss/oss/scripts/github_actions/docker-unittest.py` over the tarball.

### What public CI does and does not prove

**Does**: the fake-SAI build compiles for each target a workflow builds, and
the swept unit tests in that package pass. A workflow that builds a
services-only target runs no tests, and the sweep skips the `-fake` agent
tests; see [testing.md](testing.md#running-the-no-hardware-tests).

**Does not**:

- exercise any vendor SDK. No workflow builds against one, so there is no
  public signal that SDK integration still works.
- run on hardware. All `*_hw_test` binaries are excluded from the sweep.

### Contributing

- Install the lint hooks before your first PR:

  ```bash
  pip install -r requirements-dev.txt
  pre-commit install
  pre-commit run
  ```

  CI lints only the changed range, but the hook set (clang-format,
  shellcheck, shfmt, ruff, and assorted file hygiene checks) is the same.

- Reviews are routed by `CODEOWNERS` per subdirectory.
- A PR-title check fails any PR whose title does not begin with a bracketed
  prefix, normally your company name, for example `[CompanyName] Fix X`.

## Why internal CI exists as well

The maintainers run additional continuous builds that are not visible here,
because they need things this repository cannot have: licensed vendor SDKs and
real switches. Those builds are what produce the validated dependency pins you
consume as `fboss/oss/stable_commits/latest_stable_hashes.tar.gz`, and they are
the reason that file can be trusted as a known-good combination.

The practical asymmetry to be aware of: the pre-merge signal internally builds
the internal source layout, while this repository's layout is produced by the
export. A change can therefore pass internal pre-merge checks and still break
the build here, usually because a file was referenced that does not export.
The public CI and the maintainers' post-merge builds are what catch that. If
you see the public build broken shortly after an unrelated-looking change,
that is the likely mechanism, and it is worth filing.
