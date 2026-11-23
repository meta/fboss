# ASIC Config Generation

# Introduction
This document gives a very brief overview of the ASIC config generation process.

## ASIC Config Generation Workflow
### Asic Config Generation Tool
We provide a script to help external FBOSS vendors generate their respective ASIC config contents.

#### Prerequisites

Python 3 is required in order to run the helper script. You can install it via one of the below commands depending on which Linux distribution you're on.

##### Debian

```shell
sudo apt update
sudo apt -y upgrade
sudo apt install python3
```

##### CentOS

```shell
sudo dnf upgrade -y
sudo dnf install python3
```

#### Instructions
Platform mapping config generation is done via running the helper script below from the root of the FBOSS repository. By default, this runs on all platforms which have configs specified in [asic_config_v2/all_asic_config_params.py](https://github.com/meta/fboss/blob/main/fboss/lib/asic_config_v2/all_asic_config_params.py).

```shell
$ ./fboss/lib/asic_config_v2/run-helper.sh
```

You will need to add support for your specific platform in [asic_config_v2/all_asic_config_params.py](https://github.com/meta/fboss/blob/main/fboss/lib/asic_config_v2/all_asic_config_params.py#L10) and [asic_config_v2/gen.py](https://github.com/meta/fboss/blob/main/fboss/lib/asic_config_v2/gen.py#L17).

### Generating Agent Config from ASIC Config
Once your ASIC config is generated in [generated_asic_configs/](https://github.com/meta/fboss/tree/main/fboss/lib/asic_config_v2/generated_asic_configs), you can use our [Agent Config Helper Script](https://github.com/meta/fboss/blob/main/fboss/lib/asic_config_v2/generate_agent_config_from_asic_config.py) to create an agent config for testing.

Please follow the usage instructions in that file.
