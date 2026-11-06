# By default, `build-helper.py` will use SAI version 1.19.0. If you are planning
# on building against a different version of SAI, you must add another param to
# the build-helper.py command

# Supported values:

# 1.13.2
# 1.14.0
# 1.15.0
# 1.15.3
# 1.16.0
# 1.16.1
# 1.16.3
# 1.17.1
# 1.18.1
# 1.19.0

# Run the build helper to stage the SDK in preparation for the build step
./fboss/oss/scripts/build-helper.py /opt/sdk/lib/libsai_impl.a /opt/sdk/include/ /var/FBOSS/sai_impl_output

# Run the build helper using SAI version 1.15.3 instead of the default 1.19.0
./fboss/oss/scripts/build-helper.py /opt/sdk/lib/libsai_impl.a /opt/sdk/include/ /var/FBOSS/sai_impl_output 1.15.3
