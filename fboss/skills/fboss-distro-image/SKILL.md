---
name: fboss-distro-image
description: Interactively plan, explain, and optionally execute FBOSS OSS Distro image workflows. Default to providing commands without running them; execute only when the user explicitly asks. Use whenever a user asks about the `fboss-image` CLI, distro manifests, kernel/BSP/SAI/FBOSS component integration, USB/PXE/ONIE artifacts, `distro_infra`, or distro-image build failures. Use `--help` to see common prompts.
argument-hint: --help | plan | command | execute | manifest | components | provision | troubleshoot
user-invocable: true
allowed-tools: Bash, Read
---

# FBOSS Distro Image

## --help: Prompt Catalog

**IMPORTANT: Check `$ARGUMENTS` exactly. If `$ARGUMENTS` is literally the
string `--help` (and nothing else), read [PROMPTS.md](PROMPTS.md), present its
contents as a numbered menu, ask the user to pick an option by number, collect
any `{placeholders}`, and proceed with the selected prompt. For every other
value, including empty input, skip this section and continue below.**

## Interaction modes

Start in **advisory mode**. Reading relevant files is allowed, but do not run
shell commands, create or edit manifests, start containers, build components,
or provision devices. Instead, explain the plan and provide copy-paste commands
with their working directory, expected outputs, and risks.

Enter **execution mode** only when the user explicitly asks to run or execute a
command, build it now, or make the file change. Requests such as "help me
build," "how do I build," "prepare the build," or "give me the command" remain
advisory. Before executing, repeat the exact command and obtain confirmation
for an expensive build. Destructive actions always require confirmation of the
exact device or switch even when execution mode is already active.

## Advisory response format

End advisory responses with:

```text
Mode: advisory (no commands executed)
Working directory: <path>
Inputs or decisions still needed: <items or none>
Commands to run: <ordered copy-paste commands>
Expected outputs: <paths>
To execute: ask me to run the command(s).
```

## Start by classifying the request

Choose the narrowest matching workflow:

1. Explain or plan a complete image or selected components.
2. Explain, review, or author a manifest.
3. Integrate kernel, BSP, NPU/PHY SAI, or FBOSS stack artifacts.
4. Plan USB, PXE, or ONIE provisioning.
5. Diagnose a failed build or provisioning attempt.

Ask only for information that cannot be read from the checkout, manifest, or
error output. For a build, identify the environment, manifest, selected
components, requested output formats, and output directory before constructing
the command.

## Build a specific distro

Do not stop after pointing at documentation. In advisory mode, drive the
request to a ready-to-run command with a complete manifest or a precise blocker
naming the missing artifact or compatibility decision. In execution mode,
continue to a verified build.

1. Locate the repository root from the available context and read the CLI
   implementation or supplied help text. Read the supplied manifest. If none was
   supplied, inspect `fboss-image/from_source.json`,
   `fboss-image/manifests/generic.json`, and the platform manifests under
   `fboss-image/manifests/` to find the closest base.
2. Build a component plan containing the target platform, kernel, BSPs, NPU
   SAI, optional PHY SAI, platform stack, forwarding stack, image formats,
   artifact source, and output directory. Fill it from files available to read;
   express unresolved environment checks as commands for the user.
3. Ask one grouped follow-up for only unresolved choices. A platform name alone
   does not determine the vendor SAI, BSP, kernel ABI, or artifact locations.
4. Use an existing matching manifest unchanged when possible. Otherwise draft
   a complete manifest with real `download` or `execute` inputs; never invent an
   artifact URL or silently substitute fake SAI for a hardware image.
5. Present the completed component plan, ABI checks, exact build command, and
   expected output paths. Stop there in advisory mode. In execution mode, run
   the build after any required confirmation, then verify the requested files
   and hashes.

Read [references/build-a-specific-distro.md](references/build-a-specific-distro.md)
for the decision tree, input worksheet, concrete commands, and result format.

## References

| Need | Reference |
|---|---|
| Turn a platform request into a buildable manifest and command | `references/build-a-specific-distro.md` |
| Build complete images or selected components | `references/build-and-artifacts.md` |
| Understand manifests and component contracts | `references/manifest-and-components.md` |
| Provision with USB, PXE, or ONIE | `references/provisioning.md` |
| Diagnose failures | `references/troubleshooting.md` |

Read every reference needed for the user's request before answering or acting.
When behavior and prose documentation disagree, verify the current CLI or
implementation and report the discrepancy.

## Interaction contract

- For explanation-only requests, answer directly with the relevant paths,
  component relationships, and commands.
- In advisory mode, provide validation and build commands but do not run them.
  Show expected artifacts and any automatically built dependencies.
- In execution mode, validate first. Expensive builds require confirmation
  before execution.
- A component-only build produces component artifacts, not a complete distro
  image. State this prominently.
- Use the exact component names `npu_sai` and `phy_sai`; `sai` is not a valid
  component name.
- Never write an image to a block device or reprovision/reboot a switch without
  explicit confirmation of the exact target and acknowledgement of data loss.
- Do not send an ONIE `.bin` to `fboss-image device ... image`; that command
  accepts PXE tarballs only.
- Finish with the selected manifest, command or explanation, expected outputs,
  verification performed, and the next safe action.
