<!-- skill-prompt-catalog: v1 -->
# FBOSS Distro Image — Prompt Catalog

---

### 1. Plan a distro for a switch or platform

Start with a target and produce a build-ready plan and commands without running
them.

```
Plan an FBOSS OSS distro for {switch_or_platform}. Find the closest public
manifest, determine which kernel/BSP/SAI/FBOSS artifacts are available, ask me
only for missing inputs, and provide the proposed manifest, exact commands, and
expected {usb_pxe_or_onie} artifacts. Do not execute commands.
```

---

### 2. Prepare commands for an existing manifest

Inspect a supplied manifest and prepare the complete image build without
running it.

```
Prepare the FBOSS OSS distro build described by {manifest_path}. Show the
resolved component plan, compatibility checks, exact command, and verification
commands. Do not execute anything.
```

---

### 3. Prepare selected component commands

Prepare one or more component builds without composing an image.

```
Prepare commands to build these FBOSS distro components from {manifest_path}:
{component_names}. Explain dependencies and output locations without running
the commands.
```

---

### 4. Explain or review a manifest

Explain the schema, validate a manifest, or help author one.

```
Review {manifest_path}. Explain every component, its source, dependencies,
artifact contract, output formats, and any correctness risks.
```

---

### 5. Integrate a component

Add or replace a kernel, BSP, SAI, platform-stack, or forwarding-stack input.

```
Help integrate {component_type} into an FBOSS distro manifest. The artifact or
build script is {artifact_or_script}. Explain the required archive layout and
compatibility checks.
```

---

### 6. Plan switch provisioning

Choose the appropriate USB, PXE, or ONIE workflow without executing it.

```
Plan provisioning for {switch_or_platform} using {usb_pxe_or_onie} and image
{image_path}. Provide verification and provisioning commands without running
them.
```

---

### 7. Troubleshoot a failure

Diagnose a build, component integration, or provisioning problem.

```
Diagnose this FBOSS distro image failure using the manifest, build log, and
artifact contracts: {error_or_log_path}
```

---

### 0. Custom

Describe the distro-image question or workflow you need.

```
(Type your request here)
```
