#!/bin/bash

# Called by dnsmasq when a TFTP transfer completes. This is used to stop offering PXE boot to the given MAC so that a
# subsequent reboot does not PXE-boot again.

set -euo pipefail

action=$1
if [[ $action != "tftp" ]]; then
  exit 0
fi

# Document the script arguments even though we ignore some of them
#shellcheck disable=SC2034
filesize=$2
#shellcheck disable=SC2034
address=$3
filepath=$4

case "$filepath" in
/distro_infra/persistent/*/pxeboot_complete)
  mac=$(echo "$filepath" | cut -d/ -f4)
  # Prevent PXE boot from succeeding if there is an external authoritative DHCPv6 server with an unchanging config
  rm /distro_infra/persistent/$mac/ipxev*.efi
  # Stop offering PXE boot but keep serving DHCPv6 addresses for the installer and FBOSS
  colonmac=$(echo "$mac" | tr - :)
  echo "${colonmac},id:*,set:fbossdut,set:pxedone" >"/distro_infra/dnsmasq_conf.d/$mac"
  killall -HUP dnsmasq
  echo "$mac PXE booted, disabling future PXE boot provisioning"
  ;;
esac

exit 0
