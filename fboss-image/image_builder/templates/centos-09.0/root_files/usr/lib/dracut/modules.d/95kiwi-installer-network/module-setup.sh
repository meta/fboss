#!/bin/bash

check() {
    [[ " ${add_dracutmodules:-} " == *" kiwi-dump "* ]] || return 1
    return 0
}

depends() {
    echo network-manager
}

install() {
    inst_simple "${moddir:?}/NetworkManager.conf" \
        /usr/lib/NetworkManager/conf.d/90-kiwi-installer.conf
}
