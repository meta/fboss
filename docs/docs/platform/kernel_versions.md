# Supported and Planned Kernel Versions

This page is the single source of truth for the Linux kernel versions that FBOSS
BSP kernel modules must support. This page should be used to plan kmod
builds and qualification against current and upcoming kernels.

## Kernel Versions Status

FBOSS maintains multiple kernel versions in the production fleet at a time
(the current version and the previous one). kmods must be compatible with the
following kernel versions (updated in October 2026):

| Kernel Version | Status             |
|----------------|--------------------|
| Linux v6.4.3   | Actively Supported |
| Linux v6.11.1  | Actively Supported |
| Linux v6.16.1  | Actively Supported |
| Linux v7.1.3   | Evaluating         |

Status definitions:

- **Actively Supported**: Running in the production fleet. kmods must be fully
  compatible and pass all test requirements.
- **Evaluating**: Under active evaluation for adoption. Vendors should begin
  building and qualifying kmods so issues are surfaced early.
- **Planned**: Targeted for future adoption. Exact patch version and timelines
  are subject to change; vendors should begin planning kmod builds and
  qualification ahead of the migration.

## What "Compatible" Means

Being compatible with a kernel version means:

1. The kernel modules can be compiled against the kernel version successfully.
2. The kernel modules can be loaded in the kernel version and behave as
   expected.

## Testing Requirements

The per-driver test requirements that kmods must pass on each supported kernel
version are defined in the
[FBOSS BSP Kernel Module Development Requirements](./bsp_development_requirements.md)
document (see the "Test Requirement(s)" subsections under each driver type).
