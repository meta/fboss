#!/bin/bash
# Enable PXE boot for a device MAC address
# This script is run inside the distro_infra container

set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 <mac_address>" >&2
  echo "  mac_address: MAC address in dash format (aa-bb-cc-dd-ee-ff)" >&2
  exit 1
fi

DASH_MAC="$1"

# Validate dash format MAC address
if ! echo "$DASH_MAC" | grep -qE '^[0-9a-f]{2}(-[0-9a-f]{2}){5}$'; then
  echo "Error: Invalid MAC address: $DASH_MAC" >&2
  echo "Expected format: aa-bb-cc-dd-ee-ff" >&2
  exit 1
fi

echo "Enabling PXE boot for MAC address: $DASH_MAC"

MAC_DIR="/distro_infra/persistent/$DASH_MAC"
mkdir -p "$MAC_DIR"
chmod 777 "$MAC_DIR"

CACHE_DIR="/distro_infra/persistent/cache"

for filename in ipxev4.efi ipxev6.efi autoexec.ipxe; do
  SRC="$CACHE_DIR/$filename"
  DST="$MAC_DIR/$filename"
  rm -f "$DST"
  ln "$SRC" "$DST"
done

echo 1 >"$MAC_DIR/pxeboot_complete"

INTERFACE_FILE="/distro_infra/persistent/interface_name.txt"
if [ ! -f "$INTERFACE_FILE" ]; then
  echo "Error: Interface file not found: $INTERFACE_FILE" >&2
  exit 1
fi

INTERFACE=$(cat "$INTERFACE_FILE")
if [ -z "$INTERFACE" ]; then
  echo "Error: Interface file is empty: $INTERFACE_FILE" >&2
  exit 1
fi

IPV6=$(ip -6 addr show dev "$INTERFACE" scope global | awk -F '[[:space:]/]+' '/inet6/{print $3; exit}')
if [ -n "$IPV6" ]; then
  cat >"$MAC_DIR/ipxev6.efi-serverip" <<EOF
#!ipxe
set server_ip [$IPV6]
imgexec autoexec.ipxe
EOF
  echo "Created IPv6 serverip script with IP: $IPV6"
fi

mkdir -p /distro_infra/dnsmasq_conf.d
colonmac=$(echo ${DASH_MAC} | tr - :)
echo "${colonmac},id:*,set:fbossdut" >"/distro_infra/dnsmasq_conf.d/$DASH_MAC"

echo "PXE boot enabled for $DASH_MAC"
echo "MAC directory: $MAC_DIR"
