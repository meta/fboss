Facebook Open Switching System (FBOSS)
--------------------------------------

# test

FBOSS is Facebook's software stack for controlling and managing network
switches.

## Components

FBOSS consists of a number of user-space applications, libraries, and
utilities.

The initial open source release of FBOSS consists primarily of the agent
daemon, but we are working on releasing additional pieces and functionality as
well.

### Agent Daemon

One of the central pieces of FBOSS is the agent daemon, which runs on each
switch, and controls the hardware forwarding ASIC.  The agent daemon sends
forwarding information to the hardware, and implements some control plane
protocols such as ARP and NDP.  The agent provides thrift APIs for managing
routes, to allow external routing control processes to get their routing
information programmed into the hardware forwarding tables.

The code for the agent can be found in fboss/agent

The agent requires a JSON configuration file to specify its port and VLAN
configuration.  Some sample configuration files can be found under
fboss/agent/configs.  These files are not really intended for human
consumption--at Facebook we have tooling that generates these files for us.

### Routing Daemon

The FBOSS agent manages the forwarding tables in the hardware ASIC, but it
needs to be informed of the current routes via thrift APIs.

Our initial open source release does not yet contain a routing protocol daemon
capable of talking to the agent.  The routing protocol daemon we use at
Facebook is rather specific to our environment, and likely won't be as useful
to the open source community.  For more general use outside of Facebook, it
should be possible to modify existing open source routing tools to talk to the
FBOSS agent, but we have not implemented this yet.  In the meantime, we have
included a small sample python script in fboss/agent/tools that can manually
add and remove routes.

### Management Tools

Obviously additional tools and utilities are required for interacting with the
FBOSS agent, reporting its status, generating configuration files, and
debugging issues.

At the moment we do not have many of our tools ready for open source release,
but we hope to make more of these available in the future weeks.  In the
meantime, the thrift compiler can automatically generate a python-remote script
to allow manual invocation of the agent's various thrift interfaces.

## Building

See the BUILD.md document for instructions on how to build FBOSS.

## Future Development

FBOSS has been designed specifically to handle the needs of Facebook's data
center networks, but we hope it can be useful for the wider community as well.
However, note that this initial release of FBOSS will likely require
modification and additional development to support other network configurations
beyond the features used by Facebook.  Until it matures more, FBOSS will likely
be primarily of interest to network software developers, rather than to network
administrators who are hoping to use it as an turnkey solution.

We look forward to getting feedback from the community, and we hope FBOSS can
serve as a jumping-off point for other users wishing to program network
switches.

FBOSS development is ongoing at Facebook, and we plan to continue releasing
more components, additional features, and improvements to the existing tooling.

## License

See [LICENSE](LICENSE).
