Name:           flashrom
Version:        1.7.0
Release:        1
Summary:        Utility for reading, writing, verifying and erasing flash chips
License:        GPL-2.0-only
URL:            https://www.flashrom.org/
Source0:        flashrom-v%{version}.tar.xz

# Disable automatic debuginfo/debugsource subpackages. This is a small
# helper RPM built inside the fboss-image container; we don't need
# separate -debuginfo/-debugsource packages, and the default macros can
# fail with an empty debugsourcefiles.list.
%global debug_package %{nil}
%global _enable_debug_packages 0
%global _no_debugsource 1

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconfig(libusb-1.0)
BuildRequires:  pkgconfig(libftdi1)
BuildRequires:  pkgconfig(libpci)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(zlib)

%description
flashrom is a utility for detecting, reading, writing, verifying and erasing
flash chips. It is often used to flash BIOS/EFI/coreboot/firmware images
in-system using a supported mainboard, but it also supports flashing of
network cards, SATA controller cards, and other external devices which can
program flash chips.

%prep
%setup -q -n flashrom-v%{version}

%build
# -Dtests=disabled: flashrom 1.7 bundles cmocka 1.1.5 which fails to compile
# under GCC 11 with -Werror=clobbered. Tests are not needed for packaging.
meson setup builddir --prefix=/usr -Dtests=disabled
meson compile -C builddir

%install
rm -rf "%{buildroot}"
meson install -C builddir --destdir="%{buildroot}"

# Upstream installs the binary into /usr/sbin. We also want a
# /usr/bin/flashrom for tools (like fw_util) that expect it there.
mkdir -p "%{buildroot}/usr/bin"
ln -sf ../sbin/flashrom "%{buildroot}/usr/bin/flashrom"

%files
/usr/sbin/flashrom
/usr/bin/flashrom
/usr/include/libflashrom.h
/usr/lib64/libflashrom.a
/usr/lib64/libflashrom.so
/usr/lib64/libflashrom.so.1
/usr/lib64/libflashrom.so.1.0.0
/usr/lib64/pkgconfig/flashrom.pc
/usr/share/bash-completion/completions/flashrom.bash
