# Copyright (c) 2004-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.
#

# Required: the SAI release the modules were built from, e.g. 14.2.0
%{?!sai_version:%{error:sai_version not provided. Provide it with --define 'sai_version X.Y.Z'}}
# Required: the SDK variant, e.g. xgs_6_5_34. Becomes part of Release so that
# `rpm -qi sai-runtime` on a switch reports which SDK the image was built with.
%{?!sai_variant:%{error:sai_variant not provided. Provide it with --define 'sai_variant <variant>'}}
# Required: directory holding the .ko files produced by the kmod build
%{?!ko_dir:%{error:ko_dir not provided. Provide it with --define 'ko_dir <path>'}}

# Default in-container source dir (override via --define sai_runtime_dir if needed)
%{!?sai_runtime_dir:%global sai_runtime_dir /src/fboss-image/sai-runtime}

# Disable debug packages, and do not strip -- stripping breaks module loading
%global debug_package %{nil}
%global __strip /bin/true

# Spelled out rather than using %{_unitdir}/%{_modprobedir}, which come from
# systemd-rpm-macros. The builder image is a prebuilt artifact that does not
# carry it, and it cannot be installed at build time -- the build container has
# no route to the CentOS mirrors.
%global sai_unitdir /usr/lib/systemd/system
%global sai_modprobedir /usr/lib/modprobe.d

Name: sai-runtime
Version: %{sai_version}
Release: 1.%{sai_variant}
Summary: SAI kernel modules and boot-time loader for FBOSS (%{sai_variant})
License: Proprietary

%description
The SAI kernel modules for FBOSS built from SAI %{version} against the
%{sai_variant} SDK, together with the systemd unit and loader script that
insert them and create their device nodes at boot.

%prep
# Nothing to unpack -- the .ko files are built by the kmod job and passed in
# via ko_dir, and the unit, drop-in and scripts come from the repo.

%build

%install
# The modules live outside the distro-managed module tree; the loader copies
# them under /lib/modules/$(uname -r) at boot, where modprobe can find them.
mkdir -p %{buildroot}/usr/local/lib/modules
install -m 0644 %{ko_dir}/*.ko %{buildroot}/usr/local/lib/modules/

mkdir -p %{buildroot}/usr/local/bin
install -m 0755 %{sai_runtime_dir}/scripts/sai-load-modules.sh \
    %{buildroot}/usr/local/bin/

mkdir -p %{buildroot}%{sai_unitdir}/sysinit.target.d
install -m 0644 %{sai_runtime_dir}/systemd/sai-device-nodes.service \
    %{buildroot}%{sai_unitdir}/
install -m 0644 %{sai_runtime_dir}/systemd/sysinit.target.d/10-sai.conf \
    %{buildroot}%{sai_unitdir}/sysinit.target.d/

mkdir -p %{buildroot}%{sai_modprobedir}
install -m 0644 %{sai_runtime_dir}/modprobe.d/sai.conf %{buildroot}%{sai_modprobedir}/

%postun
# The loader's copies under /lib/modules are not owned by this package, so
# they outlive it unless removed here.
if [ $1 -eq 0 ]; then
    rm -rf /lib/modules/*/extra/sai
fi

%files
%defattr(-,root,root)
/usr/local/lib/modules/*.ko
/usr/local/bin/sai-load-modules.sh
%{sai_unitdir}/sai-device-nodes.service
%{sai_unitdir}/sysinit.target.d/10-sai.conf
%{sai_modprobedir}/sai.conf

%changelog
* Mon Sep 15 2026 FBOSS Distro Team <fboss_distro@meta.com> - %{version}-1
- Initial Meta-built sai-runtime package
