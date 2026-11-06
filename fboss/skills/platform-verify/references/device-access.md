# Device Access

How to run commands on the switch and copy files to it.

## Prerequisites

SSH access to the switch as a user who can restart services and load kernel
modules. Verify with:

```bash
ssh root@<switch> "hostname"
```

> **Customization point**: if your environment uses a different access method —
> a console server, a lab management API, an MCP integration — create
> `facebook/device-access.md` in this skill directory describing it. The skill
> prefers that file when it exists.

## Operations

`UPLOAD <local-file> TO <switch>:<remote-file>`

```bash
scp <local-file> root@<switch>:<remote-file>
```

`RUN ON <switch>: <command>`

```bash
ssh root@<switch> "<command>"
```

Capture long output to a file rather than to the terminal — a
`platform_manager` run is often over 200KB:

```bash
ssh root@<switch> "/tmp/platform_manager --reload_kmods 2>&1" > /tmp/pm_out.txt
```

## Platform name

Resolve it from the BIOS:

```bash
ssh root@<switch> "dmidecode -s system-product-name"
```

Some deployments cache the resolved name in a file on the switch. If yours
does, reading that file is faster, but `dmidecode` is always authoritative.
