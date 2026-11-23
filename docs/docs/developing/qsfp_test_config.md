# QSFP Hardware Test Config
The test configuration required to run QSFP hardware tests is modeled as the `QsfpServiceConfig` Thrift structure, which is defined [here](https://github.com/meta/fboss/blob/main/fboss/qsfp_service/if/qsfp_service_config.thrift).
## Key Fields
### QsfpTestConfig
The `QsfpTestConfig` struct contains the topology information for your switch.
```thrift
struct QsfpTestConfig {
  1: list<CabledTestPair> cabledPortPairs;
  2: TransceiverFirmware firmwareForUpgradeTest;
}
```
# Example Configuration

For example, if port `eth1/1/1` is connected to `eth1/3/1` in `400G-4` optical mode, then `QsfpTestConfig` will be:
```json
"qsfpTestConfig": {
 "cabledPortPairs": [
   {
     "aPortName": "eth1/1/1",
     "zPortName": "eth1/3/1",
     "profileID": 38
   }
 ]
}
```
> **Note:** The `profileID` field is the `PortProfileID` enum defined in `switch_config.thrift` [here](https://github.com/meta/fboss/blob/main/fboss/agent/switch_config.thrift).

## General Guidelines for Generating QSFP Hardware Test Config

1. Use the QSFP hardware test config of an existing platform as a starting point — [QSFP Test Configs](https://github.com/meta/fboss/tree/main/fboss/oss/qsfp_test_configs).
2. Replace the `mode` field in the `defaultCommandLineArgs` with the name of the platform you are testing.
3. Replace the `cabledPortPairs` field according to your topology.
4. Run tests with the new config.
