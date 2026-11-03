# Transceiver Vendors

Vendor names you may see in an onboarding request, and the part-number prefix
each one stamps on its modules. Use this to recognise which vendor the user
means before parsing the rest of the prompt.

| Vendor | Part number prefix |
|--------|--------------------|
| Eoptolink | `EOLO-` |
| Coherent | `FTCF`, `FTCE` |
| Innolight | `T-` |
| Arista | |
| Credo | Active copper (AEC) |
| Amphenol | |
| Nokia | |
| SourcePhotonics | |
| Acacia | ZR / coherent optics |
| Marvell | |
| Intel | |
| Molex | Active copper (AEC) |

**Coherent was formerly Finisar** (acquired by II-VI, then renamed). Both names
refer to the same vendor, and older code and part numbers still say Finisar.

## Vendor does not affect the Phase 1 files

`transceiver.thrift` and `TransceiverPropertiesDefault.h` are keyed by
`MediaInterfaceCode` — the media type — not by vendor. Two vendors shipping the
same media type share one entry; a new vendor for an already-supported media
type needs no Phase 1 change at all.

Vendor determines where firmware images live and how they are named, which is
Phase 2. See the reference routing table in `SKILL.md`.
