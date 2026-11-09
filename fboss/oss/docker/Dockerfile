FROM quay.io/centos/centos:stream9

# Build arguments for user creation
ARG USERNAME=root
ARG USER_UID=0
ARG USER_GID=0
ARG USER_SHELL=/bin/bash
ARG USE_CLANG=true
ARG FBOSS_META_ENV=0
ARG GITHUB_ROOT=.
ARG FBCODE_BUILDER_PATH=build/fbcode_builder
ARG http_proxy=
ARG https_proxy=
ENV http_proxy=${http_proxy} \
    https_proxy=${https_proxy}
RUN if [ -n "$http_proxy" ]; then \
      echo "proxy=$http_proxy" >> /etc/dnf/dnf.conf; \
    fi

# Download and install sccache
RUN version=v0.11.0 && \
    curl -L https://github.com/mozilla/sccache/releases/download/$version/sccache-$version-x86_64-unknown-linux-musl.tar.gz -o sccache.tar.gz && \
    tar xzf sccache.tar.gz && \
    mv sccache-$version-x86_64-unknown-linux-musl/sccache /usr/local/bin/ && \
    rm -rf sccache-$version-x86_64-unknown-linux-musl sccache.tar.gz

# Mount point for extra files.
RUN mkdir -p /var/extras

RUN dnf install -y 'dnf-command(config-manager)' && \
    dnf config-manager --set-enabled crb && \
    dnf install -y epel-release epel-next-release && \
    dnf install -y --allowerasing \
    autoconf automake bison flex gdb glibc-devel make \
    pkgconf pkgconf-m4 pkgconf-pkg-config redhat-rpm-config rpm-build rpm-sign strace \
    byacc diffstat intltool ltrace patchutils \
    pesign valgrind valgrind-devel \
    bzip2 bzip2-devel cmake double-conversion double-conversion-devel gperf \
    libcurl-devel libcurl-minimal libdwarf libdwarf-devel libevent-devel \
    libffi libffi-devel libnghttp2 libnghttp2-devel libnl3 libnl3-devel \
    jemalloc jemalloc-devel \
    libsodium-devel libsodium-static libunwind libunwind-devel libusb libusb-devel \
    libzstd libzstd-devel lz4-devel ncurses-devel ninja-build \
    openssl openssl-devel openssl-libs \
    pcre-devel \
    re2 re2-devel snappy-devel xxhash-devel xz-devel zlib-devel zlib-static \
    libxml2-devel expat-devel wget git sudo lsof `basename "$USER_SHELL"` \
    libcap-devel libmount-devel libpciaccess-devel

RUN if [ "$FBOSS_META_ENV" = "1" ]; then \
      dnf install -y podman-docker fuse-overlayfs && \
      sed -i 's|#mount_program = "/usr/bin/fuse-overlayfs"|mount_program = "/usr/bin/fuse-overlayfs"|' /etc/containers/storage.conf; \
    fi

# Set up Python 3.12 as the system default.
# Python 3.12 is chosen to match the getdeps python manifest which installs
# python3.12 on CentOS Stream 9 (the default python3 is 3.9, too old).
# All -python variant dependency builds (folly-python, fbthrift-python, etc.)
# must use the same Python version consistently.
RUN dnf install -y python3.12 python3.12-devel python3.12-pip && \
    update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.12 100
RUN ln -sf /usr/bin/python3 /usr/bin/python && \
    ln -sf /usr/bin/pip3.12 /usr/bin/pip

# Necessary for using update-alternatives.
RUN rpm -e binutils --nodeps

# Set up gcc.
RUN dnf install -y --allowerasing gcc-toolset-12 gcc-toolset-12-libasan-devel gcc-toolset-12-libatomic-devel gcc-toolset-12-libubsan-devel && \
    update-alternatives --install /usr/bin/gcc gcc /opt/rh/gcc-toolset-12/root/usr/bin/gcc 100 \
    --slave /usr/bin/g++ g++ /opt/rh/gcc-toolset-12/root/usr/bin/g++ \
    --slave /usr/bin/c++ c++ /opt/rh/gcc-toolset-12/root/usr/bin/g++ \
    --slave /usr/bin/cc cc /opt/rh/gcc-toolset-12/root/usr/bin/gcc \
    --slave /usr/bin/cpp cpp /opt/rh/gcc-toolset-12/root/usr/bin/cpp \
    --slave /usr/bin/ld ld /opt/rh/gcc-toolset-12/root/usr/bin/ld \
    --slave /usr/bin/ar ar /opt/rh/gcc-toolset-12/root/usr/bin/ar \
    --slave /usr/bin/nm nm /opt/rh/gcc-toolset-12/root/usr/bin/nm \
    --slave /usr/bin/ranlib ranlib /opt/rh/gcc-toolset-12/root/usr/bin/ranlib \
    --slave /usr/bin/objcopy objcopy /opt/rh/gcc-toolset-12/root/usr/bin/objcopy \
    --slave /usr/bin/objdump objdump /opt/rh/gcc-toolset-12/root/usr/bin/objdump \
    --slave /usr/bin/readelf readelf /opt/rh/gcc-toolset-12/root/usr/bin/readelf \
    --slave /usr/bin/strip strip /opt/rh/gcc-toolset-12/root/usr/bin/strip \
    --slave /usr/bin/gcov gcov /opt/rh/gcc-toolset-12/root/usr/bin/gcov \
    --slave /usr/bin/gcc-ar gcc-ar /opt/rh/gcc-toolset-12/root/usr/bin/gcc-ar \
    --slave /usr/bin/gcc-nm gcc-nm /opt/rh/gcc-toolset-12/root/usr/bin/gcc-nm \
    --slave /usr/bin/gcc-ranlib gcc-ranlib /opt/rh/gcc-toolset-12/root/usr/bin/gcc-ranlib

RUN dnf download libtool && \
    rpm -ivh --nodeps libtool-*.rpm && \
    rm -f libtool-*.rpm

RUN python3 -m pip install setuptools 'Cython>=3.0,<3.2' wheel auditwheel patchelf boto3 botocore gitpython meson jinja2 pyyaml filelock pytest pyang

COPY ${GITHUB_ROOT}/.pre-commit-config.yaml /tmp/.pre-commit-config.yaml
COPY ./fboss/oss/docker/setup_clang.sh /tmp/setup_clang.sh
RUN bash /tmp/setup_clang.sh && rm /tmp/setup_clang.sh

# Use /var/FBOSS as the set location to clone the git repository and store the outputs of the build.
WORKDIR /var/FBOSS/fboss
COPY ${FBCODE_BUILDER_PATH} /var/FBOSS/fboss/build/fbcode_builder
RUN if [ "$USE_CLANG" = "true" ]; then \
    find build/fbcode_builder/manifests -maxdepth 1 -type f -exec sed -i 's/^binutils/#binutils/' {} +; \
    fi && \
    ./build/fbcode_builder/getdeps.py install-system-deps --recursive fboss && \
    rm -rf build

# The following libraries are needed for building SDK
RUN dnf install -y doxygen aspell aspell-en libyaml-devel vim-common libnsl libnsl2-devel glibc-static libstdc++-static hostname \
    grpc-devel grpc-plugins protobuf-devel protobuf-compiler abseil-cpp-devel

# Install libyaml.a for SDK purposes
RUN git clone https://github.com/yaml/libyaml.git && \
    cd libyaml && \
    ./bootstrap && \
    ./configure --enable-static --disable-shared && \
    make -j && \
    cp src/.libs/libyaml.a /lib64/ && \
    cd .. && rm -rf libyaml

# Install Perl modules
RUN dnf install -y perl-diagnostics perl-sort perl-English perl-Clone perl-Data-Compare \
    perl-List-MoreUtils perl-Moose perl-YAML perl-namespace-autoclean perl-JSON-XS perl-JSON perl-FindBin \
    perl-Math-BigInt perl-YAML-LibYAML perl-Sys-Hostname perl-Time

# ============================================================
COPY ${GITHUB_ROOT}/requirements-dev.txt /tmp/requirements-dev.txt
RUN python3 -m pip install -r /tmp/requirements-dev.txt && rm /tmp/requirements-dev.txt

# Configure sudoers to allow wheel group passwordless sudo
RUN echo "%wheel ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Create user if USERNAME is not root
RUN if [ "$USERNAME" != "root" ] && [ "$USER_UID" != "0" ]; then \
    groupadd -g $USER_GID $USERNAME && \
    useradd -l -m -u $USER_UID -g $USER_GID -G adm,wheel,users -s $USER_SHELL $USERNAME && \
    chown -R $USERNAME /var/FBOSS; \
    fi

# Support sudo in a privileged container
RUN chmod 600 /etc/shadow

COPY fboss/oss/scripts/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["bash"]

USER ${USERNAME}
