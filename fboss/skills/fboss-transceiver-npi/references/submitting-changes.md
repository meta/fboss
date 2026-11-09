# Submitting Phase 1 Changes

Phase 1 touches two files, both in the open-source tree:

- `fboss/qsfp_service/if/transceiver.thrift`
- `fboss/qsfp_service/module/properties/TransceiverPropertiesDefault.h`

Keep them in a single commit — the `TransceiverPropertiesDefault.h` entry
references the enum values added to `transceiver.thrift`, so a commit with only
one of them does not build.

Suggested subject line:

```
Add <MEDIA_TYPE> transceiver support
```

The body should name the media type, the `MediaInterfaceCode` value assigned,
and the SFF-8024 table the media interface code was taken from.

Open a pull request against the FBOSS repository following `CONTRIBUTING.md`
at the root of the repository. State in the PR that both
`transceiver_properties_manager_test` and `cmis_test` pass.
