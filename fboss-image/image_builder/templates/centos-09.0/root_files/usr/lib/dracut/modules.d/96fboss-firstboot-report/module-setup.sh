#!/bin/bash

check() {
    [[ " ${add_dracutmodules:-} " == *" kiwi-dump "* ]] || return 1
}

install() {
    # 30-kiwi-dump-image.sh has finished writing and verifying the disk;
    # 40-kiwi-dump-reboot-system.sh has not yet booted it.
    inst_hook pre-mount 35 "${moddir:?}/transfer-report-args.sh"
}
