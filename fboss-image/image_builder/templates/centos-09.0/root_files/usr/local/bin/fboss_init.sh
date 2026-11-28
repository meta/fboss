#!/bin/bash
# Initialize FBOSS configuration based on platform detection

set -e

# shellcheck source=/opt/fboss/bin/setup_fboss_env
source /opt/fboss/bin/setup_fboss_env

FBOSS_SHARE="/opt/fboss/share"
COOP_DIR="/etc/coop"
FRUID_FILE="/var/facebook/fboss/fruid.json"
NPU_SDK_UTILS_TOOL="/opt/fboss/bin/npu_sdk_utils.py"
NPU_SDK_METADATA="${FBOSS_SHARE}/npu_sdk_metadata.json"
NPU_HW_AGENT_BINARY="fboss_hw_agent-sai_impl"
CHASSIS_EEPROM_PARSED="/run/devmap/eeproms/CHASSIS_EEPROM_PARSED"
# Passed as --flagfile by the fboss_sw_agent, fboss_hw_agent@ and qsfp_service
# units; on tmpfs so it cannot go stale across boots.
VARIANT_FLAGFILE="/run/fboss/variant_flags"
# Set by setup_agent_config when it creates agent.conf on this boot.
AGENT_CONF_CREATED=0

log() {
  echo "[fboss_init] $1" >&2
}

error() {
  echo "[fboss_init] ERROR: $1" >&2
}

get_platform_dir() {
  local platform
  # convert the platform name to lowercase and delete spaces
  platform=$(dmidecode -s system-product-name 2>/dev/null | tr '[:upper:]' '[:lower:]' | tr -d '[:space:]-_')
  if [[ -z $platform ]]; then
    error "Failed to get system-product-name from dmidecode"
    return 1
  fi
  log "Detected platform: $platform"

  local platform_dir="${FBOSS_SHARE}/default_configs/${platform}"
  if [[ ! -d $platform_dir ]]; then
    error "Platform config directory not found: $platform_dir"
    return 1
  fi
  log "Using platform config directory: $platform_dir"

  echo "$platform_dir"
}

copy_config() {
  local src="$1"
  local dst="$2"
  local name="$3"

  if [[ -e $dst ]]; then
    log "$name already exists at $dst (skipping)"
    return
  fi

  if [[ -f $src ]]; then
    cp "$src" "$dst"
    log "Copied $name: $src -> $dst"
  else
    log "No $name found at $src (skipping)"
  fi
}

setup_agent_config() {
  local src="$1"
  local dst="${COOP_DIR}/agent.conf"

  if [[ -e $dst ]]; then
    log "agent.conf already exists at $dst (skipping)"
    return
  fi

  if [[ ! -f $src ]]; then
    log "No agent.conf found at $src (skipping)"
    return
  fi

  # Fake-SAI artifacts intentionally do not carry NPU SDK metadata.
  if [[ ! -f $NPU_SDK_METADATA ]]; then
    log "No NPU SDK metadata found; keeping the configured SDK version"
    copy_config "$src" "$dst" "agent.conf"
    AGENT_CONF_CREATED=1
    return
  fi

  python3 "$NPU_SDK_UTILS_TOOL" materialize-agent-config \
    --config-path "$src" \
    --metadata-path "$NPU_SDK_METADATA" \
    --binary-name "$NPU_HW_AGENT_BINARY" \
    --output-directory "$COOP_DIR"
  AGENT_CONF_CREATED=1
  log "Created agent.conf with SDK versions for $NPU_HW_AGENT_BINARY"
}

generate_fruid() {
  if [[ -s $FRUID_FILE ]]; then
    log "Non-empty fruid.json already exists at $FRUID_FILE (skipping)"
    return
  fi

  mkdir -p "$(dirname "$FRUID_FILE")"

  if weutil --json >"$FRUID_FILE"; then
    log "Generated fruid.json: $FRUID_FILE"
  else
    error "Failed to generate fruid.json"
    rm -f "$FRUID_FILE"
    return 1
  fi
}

setup_coop_configs() {
  local platform_dir="$1"
  # /etc/coop is created with the right ownership, setgid bit, and default
  # ACL at image-build time (config.sh); re-assert it here in case it's ever
  # missing or drifted. The setgid bit (mode 2775) makes new entries created
  # by individual operators (via `fboss2-dev config`) inherit the switching
  # group; the default ACL forces group rwx on new entries regardless of the
  # creating operator's umask.
  mkdir -p "$COOP_DIR"
  chown coop:switching "$COOP_DIR"
  chmod 2775 "$COOP_DIR"
  setfacl -m g:switching:rwx "$COOP_DIR"
  setfacl -d -m g:switching:rwx -m o::rx "$COOP_DIR"
  setup_agent_config "${platform_dir}/agent.conf"
  copy_config "${platform_dir}/qsfp.conf" "${COOP_DIR}/qsfp.conf" "qsfp.conf"
  copy_config "${platform_dir}/led.conf" "${COOP_DIR}/led.conf" "led.conf"
}

get_eeprom_field() {
  if [[ -r $CHASSIS_EEPROM_PARSED ]]; then
    sed -n "s|^$1: *||p" "$CHASSIS_EEPROM_PARSED" | head -1
  fi
}

# Derive the platform-descriptor variant gflag (variantAttributes) from the
# chassis hardware revision, so agents never read the EEPROM themselves. A box
# missing any field gets no flag and falls back to the default descriptor.
set_variant_flags() {
  local state substate respin
  state=$(get_eeprom_field "Production State")
  substate=$(get_eeprom_field "Production Sub-State")
  respin=$(get_eeprom_field "Re-Spin/Variant Indicator")
  log "Chassis hardware revision: state=${state:-none} substate=${substate:-none} respin=${respin:-none}"

  if [[ -z $state || -z $substate || -z $respin ]]; then
    rm -f "$VARIANT_FLAGFILE"
    return
  fi

  mkdir -p "$(dirname "$VARIANT_FLAGFILE")"
  echo "--hwrev_state${state}_substate${substate}_respin${respin}=true" >"$VARIANT_FLAGFILE"
  log "Wrote $VARIANT_FLAGFILE"
}

# Start fboss2's config history (ConfigSession::initializeGit's layout) from the
# agent.conf this boot created, so its first commit is the bootstrap rather than
# whatever agent.conf holds at the first `fboss2-dev config` session.
#
# bgpd's packaged /etc/coop/bgpcpp.conf goes into the same commit as
# bgpcpp/bgpcpp.conf: fboss2's rollback removes a domain absent at the target
# commit, and bgpd aborts without its config.
commit_bootstrap_config() {
  if [[ $AGENT_CONF_CREATED -ne 1 ]]; then
    return
  fi
  # /etc/coop belongs to coop, so git refuses it to root without safe.directory,
  # which fboss2 passes on every call too.
  local git=(git -c "safe.directory=$COOP_DIR" -C "$COOP_DIR")
  if "${git[@]}" rev-parse --verify HEAD >/dev/null 2>&1; then
    log "fboss2 config history already has commits (skipping)"
    return
  fi
  # fboss2 skips its own git init once the repository exists, so this sets up
  # what that init would: group-writable files and the fboss-cli identity.
  local files=(cli/agent.conf cli/cli_metadata.json)
  local bgp_live="${COOP_DIR}/bgpcpp.conf"
  local seed_bgp=0
  if [[ -f $bgp_live && ! -L $bgp_live && ! -e "${COOP_DIR}/bgpcpp/bgpcpp.conf" ]]; then
    files+=(bgpcpp/bgpcpp.conf)
    seed_bgp=1
  fi
  if (
    umask 0002
    mkdir -p "${COOP_DIR}/cli" &&
      cp "${COOP_DIR}/agent.conf" "${COOP_DIR}/cli/agent.conf" &&
      printf '{}' >"${COOP_DIR}/cli/cli_metadata.json" &&
      {
        [[ $seed_bgp -eq 0 ]] ||
          { mkdir -p "${COOP_DIR}/bgpcpp" && cp "$bgp_live" "${COOP_DIR}/bgpcpp/bgpcpp.conf"; }
      } &&
      "${git[@]}" init --quiet --initial-branch=main --shared=group &&
      "${git[@]}" config user.name fboss-cli &&
      "${git[@]}" config user.email fboss-cli@localhost &&
      "${git[@]}" add -- "${files[@]}" &&
      "${git[@]}" commit --quiet -m "Initial commit: bootstrap agent.conf"
  ); then
    log "Committed the bootstrap config (${files[*]}) to the fboss2 config history"
  else
    # Not fatal: fboss2 starts the history itself at its first session.
    error "Failed to commit the bootstrap agent.conf to ${COOP_DIR}"
  fi
}

enable_hw_agents() {
  local platform_dir="$1"
  local num_hw_agents
  if [ ! -f "${platform_dir}/num_hw_agents" ]; then
    num_hw_agents=1
  else
    num_hw_agents=$(cat "${platform_dir}/num_hw_agents")
  fi
  for i in $(seq ${num_hw_agents}); do
    systemctl enable "fboss_hw_agent@$((i - 1)).service"
    # Calling systemctl start inside a starting systemd service deadlocks
    systemctl start "fboss_hw_agent@$((i - 1)).service" &
  done
}

create_distro_base_snapshot() {
  local base_snapshot="/distro-base"

  if [[ -e $base_snapshot ]]; then
    log "Base snapshot already exists at $base_snapshot (skipping)"
    return
  fi

  log "Creating base snapshot for service updates..."
  if btrfs subvolume snapshot / "$base_snapshot"; then
    log "Created $base_snapshot snapshot successfully"
    # Make it read-only to prevent accidental modifications
    if btrfs property set -ts "$base_snapshot" ro true; then
      log "Set $base_snapshot to read-only"
    else
      error "Failed to set $base_snapshot to read-only"
      return 1
    fi
  else
    error "Failed to create $base_snapshot snapshot"
    return 1
  fi
}

main() {
  log "Starting FBOSS initialization"

  local platform_dir
  if ! platform_dir=$(get_platform_dir); then
    exit 1
  fi

  create_distro_base_snapshot
  setup_coop_configs "$platform_dir"
  commit_bootstrap_config
  set_variant_flags
  if ! generate_fruid; then
    exit 1
  fi
  enable_hw_agents "$platform_dir"

  log "FBOSS initialization complete"
}

main "$@"
