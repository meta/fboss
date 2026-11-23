# Obtain an image tarball from the latest successful GitHub actions run
#
# CentOS
# https://github.com/meta/fboss/actions/workflows/export-centos-docker-image.yml
#
# Debian
# https://github.com/meta/fboss/actions/workflows/export-debian-docker-image.yml

# Decompress it using zstd
zstd -d fboss_debian_docker_image.tar.zst

# Load the image
sudo docker load < fboss_debian_docker_image.tar
