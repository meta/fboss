# Meta/OpenBMC Pull Request Guidelines

## Revision 0.2

The document describes the guidelines and best practices to submit pull requests
to Meta OpenBMC repository.

## License/Copyright

Meta Copyright is required for all the files submitted to OpenBMC repository.
Below is the latest copyright:

“Copyright (c) Meta Platforms, Inc. and affiliates.”

## Adopt the Latest Versions

* Always adopt the latest u-boot for OpenBMC development. The latest u-boot
  version is v2019.04 as of May 2022.
* Always adopt the latest Linux for OpenBMC development. The latest Linux
  version is v6.18 as of May 2026.
* Always adopt the latest yocto for OpenBMC development. The latest yocto
  version is “lf-dunfell” as of May 2022.

## Repository Layout: Which Layer Contains What

The openbmc tree is a collection of Yocto/bitbake layers, ordered from the most
generic (shared by every platform) to the most specific (a single machine).
Knowing which layer owns a file tells you where your change belongs, and it
also determines the `<platform/layer>` prefix of your patch title.

| Path | Scope | Typical contents |
| --- | --- | --- |
| `common/recipes-*` | All Meta OpenBMC platforms | Shared libraries, services and utilities: `recipes-lib` (libpal, libkv, libipmi, ...), `recipes-core`, `recipes-utils`, and etc. |
| `meta-aspeed` | ASPEED SoC | Everything tied to the ASPEED SoC family and nothing above it: `linux-aspeed` kernel, ASPEED u-boot, `soc-utils`, and etc. |
| `meta-nuvoton` | Nuvoton SoC | Same role as `meta-aspeed`, for the Nuvoton NPCM BMC SoC family: `recipes-kernel`, `recipes-bsp` (u-boot), and etc. Not used by any FBOSS platform as of today. |
| `meta-facebook/meta-fboss-lite` | Shared by all FBOSS BMC-Lite platforms | The BMC-Lite baseline: `conf/machine/fboss-lite.inc`, the `fblite` u-boot and kernel recipes, `ipmi-lite`, and etc. |
| `meta-facebook/meta-<platform>` | One machine | Machine-specific code only: `conf/machine/<platform>.conf`, device tree selection, platform `openbmc-utils` overrides, and etc. |
| `tests2/` | OpenBMC CIT test suite | CIT test suite for all the Meta OpenBMC platforms. |

Layer priority matters when the same recipe exists in several layers: a higher
`BBFILE_PRIORITY` wins. Today it is `6` for the top-level `openbmc` collection
(`common/`), `15` for `meta-facebook`, and `25` for the individual platform
layers — so a platform `.bbappend` always overrides the common recipe.

### Choosing the Right Layer

Put code in the *most shared* layer that all of its consumers can reach, and no
higher:

* Only one machine needs it → `meta-facebook/meta-<platform>`.
* Two or more FBOSS BMC-Lite platforms need it → `meta-facebook/meta-fboss-lite`.
* It is tied to the BMC SoC rather than the board → `meta-aspeed` or
  `meta-nuvoton`.
* Both switch and server platforms need it → `common`.

Copying a file from `common` into a platform layer just to tweak it is almost
always the wrong answer: extend the common recipe, or add a `.bbappend`.

## Prepare Your Patches

### Patch Title

Use a one-line title/subject to describe the scope and purpose of the patch. The
title must follow the below format, which is easier for people to identify the
scope and purpose of patches from “git log --oneline” output.

```plaintext
<platform/layer>: <component>: <patch title>
```

The title line should not exceed 80 columns. If you find it hard to describe the
patch within 80 columns, most likely you need to split the patch. Please
remember: solve only one problem per patch.

**Bad example:** “fix errors”

**Good example:**

* “fuji: lmsensors: fix ucd9000 compute formula in config file”
* “common: ipmid: fix file descriptor leak in foo() function”

### Patch Description

A good patch starts with a good description. The patch description should
explain why the patch is needed, or what problem to be resolved. This is helpful
to avoid generic questions like “why including the package” or “why not...”,
which may take several days to follow up and clarify.

The description lines must be wrapped at 80 columns.

**Bad example:**

```plaintext
add my-cli command
```

**Good example:**

```plaintext
add my-cli command in fuji openbmc image, and the command is used to
initialize devices when FRU PIM[1-8] are plugged.
```

### Testing Done

“Testing Done” section is mandatory for all the Pull Requests. The section
should list what tests have been executed to verify the touched code path, and
to prove the patch is safe and meet the predefined goals.

**Bad example:**

```plaintext
test passed.
```

**Good example:**

```plaintext
1) “bitbake fuji-image” succeeded.
2) run fw-util scm –version: BIC version is displayed properly
3) run fw-util scm upgrade bic <bic.bin>: command succeeded
4) power cycle the chassis and confirm BIC is healthy by running commands…
```

### Fix Linter Errors and Warnings

Fix all the errors and warnings reported by github reviewdog before publishing
pull requests to Meta for review.

## Logging

Please refer to below link for logging best practices:

[openbmc_logging.rst](https://github.com/facebook/openbmc/blob/helium/Documentation/openbmc_logging.rst)

## Dealing with Kernel Patches

### No Kernel Hacks

Meta’s goal is to upstream all the OpenBMC kernel patches to the Linux
community, so we don’t like kernel hacks.

If kernel hacks are really unavoidable for some reasons (for example, due to
improper hardware design and it’s too late to make hardware changes), please
explain the reason clearly in the patch description and code.

**For example:**

```
XXX this is a hack/workaround, and it’s needed because…

The hack can be safely dropped when following conditions are met...
```

## Use checkpatch.pl

Before sending out the kernel pull requests, please run “checkpatch.pl” and fix
errors/warnings if any. This step is mandatory when upstreaming patches to the
kernel community.

Below are the steps:

```bash
git format-patch -1
linux-aspeed-5.15/scripts/checkpatch.pl 0001-your-kernel-patch.patch
```

## No “devmem” in Production Code

“devmem” is included in the OpenBMC image for debugging purposes only. If you
feel you need “devmem” in OpenBMC services/applications, please answer the
following questions:

* Can you achieve the same goals using existing kernel ABIs (character devices,
  sysfs, etc.)?
* If the corresponding kernel ABI is not available, does it make sense to add
  interfaces to a certain driver(s) to meet your needs?

Please don’t hesitate to contact the Meta team if you need inputs.

## Contributing to Common Layer

If the package/feature is used by more than 1 platform, it should go to the “
common” layer. If you want to introduce a new layer for a specific set of
platforms, please contact Meta.

Similarly, if the common libraries/services/utilities are close to what you are
looking for, please enhance the existing software instead of reinventing the
wheel.

Although touching common code could potentially break the existing platforms,
vendors are welcome to contribute code to the common layer: we just need to pay
special attention to minimize the chance of breaking the existing
callers/consumers.

Below are 3 typical types of changes: please do not mix these types of changes
in a single pull request, because Meta team needs to involve proper people/team
to review the pull requests, depending on the scope/type of the changes.

## Adding New Features

Adding new features (packages, types, functions, etc.) to common layer is
usually safe (low risk to the existing callers/consumers), but you must take
below into consideration:

* Describe steps to enable the feature in the patch description (and/or source
  code), if the feature is disabled by default.
* If the feature is designed to be enabled automatically, please analyze and
  describe the impact to the existing callers/consumers in the patch
  description.
* Once the new feature is added, it can be very hard to delete it or change the
  interface in the future. So do NOT add something just because you feel it
  might be needed: add something only when it’s needed.
* CIT is mandatory for the new features, and this is to protect your platform
  from being broken by others’ patches.

## Enhancing Features

If it’s improving the internal implementation of the existing features, and the
external interfaces are not changed, it won’t affect the existing users.

But still, please verify if the new code paths or enhancements can be covered by
the existing test cases; if not, please add more tests.

## Modifying interfaces

If you need to modify the common library/service/application interfaces, please
grep all the references in the openbmc tree and make updates if needed.

Don't panic if something is broken, and we will guide you to fix the problems.

## Consistency

Make sure the new code is consistent across the platform and/or component. For
example:

* Coding style (indention, naming style, etc.) must be consistent across the
  same source file.
* API interface must be across the same library. For example, always returning 0
  for success, -errno on failures, etc.
* Command line interface must be consistent across the same platform.
