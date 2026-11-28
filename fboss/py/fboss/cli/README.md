# FBOSS Cli
FBOSS Cli is a tool that will allow the user to interface with an FBOSS switch
to read and write a subset of data from the switch

## Examples
_Read the contents of the ARP table_
cli.py -H <switch> arp table

_Read data about each port on the switch_
cli.py -H <switch> port status

## Requirements
FBOSS CLI is apart of the FBOSS suite. It requires that FBOSS is installed and
working. The additional requirements for FBOSS Cli are as follows:

* Python 3.5
* [FBThrift](https://github.com/meta/fbthrift) (This will be installed when you build FBOSS)
* [Click](http://click.pocoo.org/5/)
* [Six](https://pypi.python.org/pypi/six)


## Building FBOSS Cli
You must have FBOSS built and installed. This will ensure all requirements have
been met in order for FBOSS Cli to function properly.
After that there are no additional steps for build FBOSS Cli. It runs as a python
script.

## Installing FBOSS Cli
The following steps are required before FBOSS Cli will run properly.

### Install package dependencies
```
test@host:# pip install click six
```
#### Compile the necessary thrift files
```
test@host:# cd /<dir_to_fboss>/fboss/fboss/agent/if
test@host:# thrift -r -I ~/fboss -v --gen py ctrl.thrift
```
### Install thrift py libraries
```
test@host:# cd /<dir_to_fboss>/fboss/external/fbthrift/thrift/lib/py
test@host:# python3 setup.py install
```
### Run FBOSS cli and test!
```
test@host:/<dir_to_fboss>/fboss/fboss/py/fboss/cli$ ./cli.py --help
Usage: cli.py [OPTIONS] COMMAND [ARGS]...

  Main CLI options, all options are passed to children via the context obj
  "ctx" and can be accessed accordingly

Options:
  -H, --hostname TEXT    Host to connect to (default = ::1)
  -p, --port INTEGER     Thrift port to connect to
  -t, --timeout INTEGER  Thrift client timeout in seconds
  --help                 Show this message and exit.

Commands:
  arp             Show ARP Information
  aggregate_port  Show Aggregate Port Information
  config          Show running config
  interface       Show interface information for Interface(s);...
  ip              Show IP information for an interface
  l2              Show L2 information
  lldp            Show LLDP neighbors
  ndp             Show NDP information
  nic             Show host NIC information
  port            Show port information
  product         Show product information
  reloadconfig    Reload agent configuration file
  route           Show route information
```

## How FBOSS Cli works
FBOSS Cli works by making thrift calls to FBOSS daemons to read and write
information to/from the switch.

## License
FBOSS cli is BSD-licensed. We also provide an additional patent grant.
