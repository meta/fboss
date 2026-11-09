# Build Environment (Meta External)

How to build FBOSS test binaries, locate config files, and find vendor SDK artifacts.

> **For the build system itself**, the `build-fboss-oss-local` skill is the
> canonical reference: container setup, the full `run-getdeps.py` flag surface,
> supplying a vendor SAI SDK, build targets, and a troubleshooting catalogue
> indexed by error message. This file covers what an AgentHwTest workflow needs
> on top of that.

> **Customization point**: If your environment has a different build system
> or SDK layout, create `facebook/build-environment.md` in this skill
> directory with your environment-specific commands and paths. The skill
> will automatically prefer it over this file.

## Building Test Binaries

FBOSS uses CMake for open-source builds. See `fboss/oss/scripts/` for
build scripts and `fboss/oss/doc/` for setup instructions.

### Command shape

`run-getdeps.py` parses its own SDK flags **before** the getdeps subcommand
(`build`) and forwards everything after it to `getdeps.py`. Two things to get
right: the SAI-impl selector is `--npu-sai-impl` (not `--sai-impl`), and the
binary to build is chosen with getdeps' `--cmake-target` (not `--target`).
There is no `--install-dir` flag — the output location is set by
`--scratch-path`. Run `./fboss/oss/scripts/run-getdeps.py -h` for the full list.

`--npu-sai-impl` officially supports `SAI_BRCM_IMPL`, `SAI_TAJO_IMPL`, and
`CHENAB_SAI_SDK`. Omitting it builds against fake SAI.

### Mono (sai_agent_hw_test)

```bash
cd /var/FBOSS/fboss
./fboss/oss/scripts/run-getdeps.py \
  --npu-sai-impl SAI_BRCM_IMPL \
  --npu-sai-sdk-version <SAI_VERSION_...> \
  --npu-libsai-impl-path <path-to-vendor-sdk> \
  build \
  --allow-system-packages \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  fboss \
  --cmake-target sai_agent_hw_test-sai_impl
```

### Multi-switch (multi_switch_agent_hw_test + fboss_hw_agent)

Multi-switch mode requires **two** binaries. Run the same command twice,
changing only `--cmake-target`:

```bash
# 1. the test binary (SDK-independent, so no -sai_impl suffix)
./fboss/oss/scripts/run-getdeps.py \
  --npu-sai-impl SAI_BRCM_IMPL \
  --npu-sai-sdk-version <SAI_VERSION_...> \
  --npu-libsai-impl-path <path-to-vendor-sdk> \
  build --allow-system-packages \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  fboss --cmake-target multi_switch_agent_hw_test

# 2. the hw_agent binary (SDK-specific)
./fboss/oss/scripts/run-getdeps.py \
  --npu-sai-impl SAI_BRCM_IMPL \
  --npu-sai-sdk-version <SAI_VERSION_...> \
  --npu-libsai-impl-path <path-to-vendor-sdk> \
  build --allow-system-packages \
  --scratch-path /var/FBOSS/tmp_bld_dir \
  fboss --cmake-target fboss_hw_agent-sai_impl
```

### Build Output Paths

getdeps writes binaries into the build tree under the scratch path, not into an
install prefix:

```
<scratch-path>/build/fboss/sai_agent_hw_test-sai_impl
<scratch-path>/build/fboss/multi_switch_agent_hw_test
<scratch-path>/build/fboss/fboss_hw_agent-sai_impl
```

`fboss/oss/scripts/package.py --build-dir <scratch-path> forwarding-stack` tars
those into `bin/`, which unpacks to `/opt/fboss/bin/<binary>` on the device.

**Mind the `-sai_impl` suffix.** The SAI-linked binaries are declared as
`sai_agent_hw_test-${SAI_IMPL_NAME}` and `fboss_hw_agent-${SAI_IMPL_NAME}`, so
the bare names `sai_agent_hw_test` and `fboss_hw_agent` are never produced.
Only `multi_switch_agent_hw_test` is unsuffixed. A fake-SAI build yields
`sai_agent_hw_test-fake` instead of `sai_agent_hw_test-sai_impl`.

## Config Files

FBOSS ships example agent configs under:

```
fboss/oss/hw_test_configs/
```

Copy and customize for your platform. See
[build-and-load.md](build-and-load.md#config-files) for the naming
convention.

## Broadcom DNX Firmware Location

Broadcom DNX platforms require the firmware `db/` directory. Locate it
in your Broadcom SDK installation, typically under:

```
<your-bcm-sdk-path>/tools/sand/db
```

The exact path depends on your SDK version and installation layout.
Consult your Broadcom SDK release notes or installation guide.

## Leaba/Cisco SDK Libraries Location

Leaba ASIC targets require `res/` and `lib/` from the Leaba SDK.
Locate them in your SDK installation, typically:

```
<your-leaba-sdk-path>/res
<your-leaba-sdk-path>/lib
```

These may be symlinks — use `tar -h` (follow symlinks) when creating
tarballs to ensure real files are included.
