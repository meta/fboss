# fboss-sim - FBOSS Simulator Runtime

Minimal Docker runtime (~1.2 GB) for FBOSS with fake SAI — no hardware required.

## Quick Start

```bash
# 1. Build FBOSS with fake SAI (binaries land in .build_dir/build/fboss/).
#    Use the standard getdeps OSS build with the fake-SAI target.
sudo ./build/fbcode_builder/getdeps.py build --allow-system-packages fboss

# 2. Build the runtime image.
#    The packager has two phases:
#      --collect-only : resolve binaries+libs into tmp_build_dir/ (ldd must run
#                       in an OS matching the image base — CentOS Stream 9)
#      --build-only   : docker build the image from tmp_build_dir/
#    If you built on a CentOS Stream 9 host, run both at once with no flag:
python3 fboss-sim/scripts/fboss-sim-docker-package.py

# 3. Start the container
python3 fboss-sim/scripts/fboss-sim-docker-run.py

# 4. Run integration test
docker exec fboss_sim_runtime_${USER} /opt/fboss/bin/fboss2_integration_test
```

### Split build/host environments

When FBOSS is built inside a CentOS build container on a different-distro host
(e.g. Ubuntu), `ldd` must resolve libraries inside the container, but
`docker build` needs the host's Docker daemon. Run the two phases separately:

```bash
# collect inside the build container (CentOS libs resolve correctly)
docker exec <build-container> python3 \
  fboss-sim/scripts/fboss-sim-docker-package.py --collect-only

# build the image on the host
python3 fboss-sim/scripts/fboss-sim-docker-package.py --build-only
```

## Agent Modes

### Optional BGP in local mode

Pass an OSS BGP++ DC archive with `--bgp-tar`. Its root should contain `bin/bgp`
(or `sbin/bgp`) and the daemon's shared dependencies under `lib/`:

```bash
python3 fboss-sim/scripts/fboss-sim-docker-run.py --local \
  --bgp-tar /path/to/bgp.tar.gz
```

The launcher mounts and extracts the archive under `/opt/bgp`, installs
`bgpd.service`, and tracks the initial BGP configuration in `/etc/coop`.
The daemon uses plaintext Thrift on port 6909. Run the same integration suite
with or without the archive; its BGP tests skip when BGP is absent.

```bash
docker exec fboss_sim_runtime_${USER} /opt/fboss/bin/fboss2 show bgp config running
docker exec fboss_sim_runtime_${USER} /opt/fboss/bin/fboss2_integration_test \
  --gtest_output=xml:/tmp/integration-tests.xml
```

**Split mode** (default): `fboss_sw_agent` + `fboss_hw_agent@0` run as separate systemd services.
The hw_agent connects to sw_agent over IPv6 (`::1`). This requires the container to have a
non-loopback IPv6 address — `fboss-sim-docker-run.py` handles this automatically by creating
a user-defined IPv6-enabled Docker network (`fboss_sim_net_${USER}`, subnet `fd00:fb05:5::/64`).

**Monolithic mode**: single `wedge_agent` process.
```bash
sudo docker exec fboss_sim_runtime_${USER} switch-agent-mode.sh mono
```

## Useful Commands

Each agent unit writes to its own log file, so concurrent crash traces from the sw
and hw agents don't interleave into one file.

| Mode | Unit | Log file |
|------|------|----------|
| split | `fboss_sw_agent` | `/var/facebook/logs/fboss/sw_agent.log` |
| split | `fboss_hw_agent@N` | `/var/facebook/logs/fboss/hw_agent<N>.log` |
| mono | `wedge_agent` | `/var/facebook/logs/fboss/wedge_agent.log` |

```bash
# Check agent status (split mode)
sudo docker exec fboss_sim_runtime_${USER} systemctl status fboss_sw_agent
sudo docker exec fboss_sim_runtime_${USER} systemctl status fboss_hw_agent@0

# View agent logs (split mode)
sudo docker exec fboss_sim_runtime_${USER} tail -f /var/facebook/logs/fboss/sw_agent.log
sudo docker exec fboss_sim_runtime_${USER} tail -f /var/facebook/logs/fboss/hw_agent0.log

# Run FBOSS CLI
sudo docker exec fboss_sim_runtime_${USER} /opt/fboss/bin/fboss2 show interface

# Open a shell
sudo docker exec -it fboss_sim_runtime_${USER} bash
```

`fboss-sim-docker-run.py --help` prints this command list too, along with the
`--local` options.

## Architecture

```
fboss-sim/
├── docker/
│   ├── Dockerfile.runtime             # CentOS Stream 9 minimal runtime image
│   └── runtime/
│       ├── mono.conf                  # Monolithic agent config
│       ├── fruid.json                 # Platform identification (virtual env)
│       ├── setup-container.sh         # Runs at image build time: symlinks, jemalloc, services
│       └── switch-agent-mode.sh       # Toggle between split and monolithic mode
└── scripts/
    ├── fboss-sim-docker-package.py    # Collect binaries+libs (via ldd), build runtime image
    └── fboss-sim-docker-run.py        # Create IPv6 network, start runtime container
```

`Dockerfile.runtime` copies the FBOSS `centos-09.0` rootfs overlay
(`fboss-image/image_builder/templates/centos-09.0/root_files/`) for the systemd
layout — the agent units the simulator runs (`fboss_sw_agent`,
`fboss_hw_agent@`, the hw-agents target), the login-shell env hook, and the
eth0 IPv6 sysctl. That rootfs is part of the FBOSS tree, so this directory
has no external build dependencies.

### How `fboss-sim-docker-package.py` works

1. Verifies required binaries exist in `.build_dir/build/fboss/`
2. **Collect phase** (`--collect-only`, or the first half of a flagless run):
   resolves each binary's shared libraries via `ldd` and copies binaries,
   libraries, and config files into `tmp_build_dir/`. This must run where the
   resolved libs match the image base OS (CentOS Stream 9).
3. **Build phase** (`--build-only`, or the second half of a flagless run):
   builds the Docker image from `Dockerfile.runtime` using `tmp_build_dir/` as
   context, then cleans up `tmp_build_dir/`.

### Why IPv6 matters for split mode

`folly::SocketAddress::setFromLocalPort()` calls `getaddrinfo(nullptr, port, AI_ADDRCONFIG)`.
`AI_ADDRCONFIG` only returns IPv6 results if the host has at least one non-loopback IPv6 address.
Docker's default bridge sets `net.ipv6.conf.eth0.disable_ipv6=1` per-interface, so containers
get no non-loopback IPv6 → Thrift servers bind to `0.0.0.0` only → hw_agent's connection to
`::1` is refused. The user-defined network with `--ipv6` prevents this.

## Binaries Included

| Binary | Purpose |
|--------|---------|
| `wedge_agent-sai_impl` | Monolithic agent (fake SAI) |
| `fboss_sw_agent` | Split mode: SW/control plane |
| `fboss_hw_agent-sai_impl` | Split mode: HW/forwarding plane (fake SAI) |
| `fboss2` | FBOSS CLI |
| `fboss2-dev` | FBOSS dev CLI |
| `fboss2_integration_test` | CLI integration test suite |
| `setup_fboss_env` | Environment setup helper |

## Container Capabilities

- `CAP_NET_ADMIN`: TUN interface creation (TunManager)
- `CAP_SYS_ADMIN`: systemd cgroup management
- `/dev/net/tun`: virtual network interfaces
- `--shm-size=512m`: shared memory for FBOSS IPC
