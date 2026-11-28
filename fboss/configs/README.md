# FBOSS Configs

This directory contains the inputs that FBOSS services are configured from,
and the generated artifacts built from them. Generated artifacts are checked
in. It covers:

- platform mappings
- ASIC configs
- forwarding-stack (agent) configs
- platform-stack service configs

Meta and external vendors share this directory. Most files are open source.
Meta-internal files live in `facebook/` directories, which are not exported to
GitHub. The rules below fix where every file goes, so producers and consumers
always know where to look.

## Layout

```
configs/
  asic_vendors/
    common/                                 # Defaults shared by all ASIC vendors (not a vendor)
    <asic_vendor>/<family>/...              # ASIC-family defaults shared by platforms
  platforms/
    generic/                                # Shared schemas, utilities and examples (not a vendor)
    <system_vendor>/<platform>/
      platform_mapping/                     # Lane mapping, SI and profile inputs
        generated/                          # platform_mapping.json, platform_descriptor.json, ...
      asic_config/                          # asic_config.json and other ASIC-config inputs
        generated/                          # <variant>.yml or <variant>.json
      forwarding_stack/                     # config_generation.json
        link_topology/                      # <name>.csv
        generated/<service>/                # e.g. generated/agent/<profile>[_<variant>].conf
      platform_stack/                       # platform_manager.json, sensor_service.json, ...
      variants/<variant>/<component>/...    # Per-hardware-variant components; same rules
```

- `<platform>` is the platform's lowercase name (for example `montblanc`). Tools
  accept this name as `--platform`.
- A **component** is one of the directories directly under `<platform>/` or
  `variants/<variant>/`: `platform_mapping`, `asic_config`, `forwarding_stack`
  or `platform_stack`.
- A **component root** is a component directory, or an
  `asic_vendors/<asic_vendor>/` directory.

## Open-source and Meta-internal files

### Rules

1. **Mirror rule.** Removing the `facebook/` segment from a path gives the path
   the file would have if it were open source. A path contains at most one
   `facebook/` segment.

2. **`facebook/` never goes above a component root.** It may appear only at or
   below a component root. Do not place it directly under `platforms/`,
   `<system_vendor>/`, `<platform>/`, `variants/<variant>/` or `asic_vendors/`.

3. **Inputs: put `facebook/` directly beside the internal file.** A tool looks
   for each input in exactly two places: `<dir>/<file>` and
   `<dir>/facebook/<file>`. The exception is an input that is itself a nested
   tree, such as an ASIC family under `asic_vendors/`. Put one `facebook/` at
   the top of the internal subtree instead.

4. **Generated artifacts: put `facebook/` directly beside the file.** Write
   internal artifacts to `<component>/generated/[<service>/]facebook/<file>`,
   never to `facebook/generated/`. An artifact generated from any internal
   input is internal.

5. **A path exists on only one side.** A file is either open source or
   internal, not both. The exception is a file that the tool reading it lists
   as mergeable (see [Mergeable files](#mergeable-files)).

### Examples

A whole ASIC family is internal. It is a nested tree, so one `facebook/` sits
at the top of its subtree (rule 3):

```
asic_vendors/<asic_vendor>/
  facebook/
    <family>/
      <family>.json
      asics/<asic>.json
```

A component is open source except for one internal input and the artifact
built from it (rules 3 and 4):

```
asic_config/
  asic_config.json                 # open source
  facebook/
    extra_settings.txt             # internal input
  generated/
    default.yml                    # built only from open-source inputs
    facebook/
      internal_variant.yml         # built from an internal input, so internal
```

A platform whose agent configs are internal. The manifest is internal, so
every config generated from it is internal:

```
forwarding_stack/
  facebook/
    config_generation.json
  link_topology/
    link_test_default.csv          # open source: contains no vendor data
    facebook/
      link_test_internal.csv       # internal input
  generated/
    agent/
      facebook/
        hw_test.conf
        benchmark.conf
```

Do **not** put `facebook/` directly under a platform:

```
<platform>/facebook/platform_mapping/...     # wrong: breaks rule 2
<platform>/platform_mapping/facebook/...     # right
```

### Mergeable files

A mergeable file may have both an open-source part and a `facebook/` part with
the same relative path. The tool that reads the file merges the two parts. A
variant or output name defined in both parts is an error.

| File | Tool | Status |
|---|---|---|
| None yet | | |

When a tool adds merge support for a file, add the file to this table in the
same change.

## Consuming generated artifacts

A consumer finds a generated artifact by checking exactly two locations:

1. `<component>/generated/[<service>/]facebook/<file>`
2. `<component>/generated/[<service>/]<file>`

Because of rule 5, at most one of them exists. These two locations are always
enough. A consumer may search more broadly, for example discovering
descriptors recursively, but must still find every artifact in one of them.
Always derive the platform name from the `<platform>` directory.

Open-source builds only see location 2.

## Reading inputs (for tool authors)

To resolve the input `<file>` in directory `<dir>`, check two locations:

1. `<dir>/facebook/<file>`
2. `<dir>/<file>`

If both exist, fail, unless the file is mergeable. If neither exists, the
input is missing.

For an input that is a nested tree under component root `C`, such as an ASIC
family, check `C/facebook/<tree>/...` and `C/<tree>/...` instead.

## Generating artifacts

After changing an input, regenerate, then commit the inputs and generated
artifacts together.

| Component | Open-source build | Meta internal |
|---|---|---|
| `platform_mapping` | `./fboss/lib/platform_mapping_v2/run-helper.sh --platform-name <platform>` | `buck2 run @//mode/opt fbcode//fboss/lib/platform_mapping_v2/facebook:gen` |
| `asic_config` | `./fboss/lib/asic_config_v3/run-helper.sh` | `buck2 run fbcode//fboss/lib/asic_config_v3:gen -- --fboss-root <repo>/fboss` |
| `forwarding_stack` | `fboss2 config gen agent --fboss-root <repo>/fboss [--platform <platform>]` | `buck2 run fbcode//fboss/cli/fboss2:fboss2 -- config gen agent --fboss-root <repo>/fboss [--platform <platform>]` |

To check that the checked-in agent configs are up to date:

```
fboss2 config gen agent --fboss-root <repo>/fboss --verify
```

`platform_stack` configs are written by hand and have no generator.

More detail:
- [Platform mapping](https://meta.github.io/fboss/docs/developing/platform_mapping/)
- [ASIC config v3](https://meta.github.io/fboss/docs/developing/asic_config_v3/overview/)

## Adding a platform

1. Create `platforms/<system_vendor>/<platform>/` with the components the
   platform needs.
2. Put each input in the component directory, or in a `facebook/` directory
   under it, following the rules above.
3. Run each component's generator. Commit the inputs and generated artifacts
   together.
4. For agent configs, add `forwarding_stack/config_generation.json`, or
   `forwarding_stack/facebook/config_generation.json` if the platform's agent
   configs are internal, listing the profiles to generate. Then run
   `fboss2 config gen agent --platform <platform>`, followed by `--verify`.
