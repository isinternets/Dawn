# Account menu persistence

Build 86657 sends menu preferences through Web Service opcode 701, native schema
`80807603`. The old status-only route acknowledged this request without saving it.

The native producer at RVA `BDE480` compares the editable account slice against the
Family-4 account resident at offset `0x748`, then sends it through `F2BDC0` and
`530890`. Its `0x9C0`-byte native request contains `8080781F`, with account settings
under `80807820`. The settings record (`80807827`) begins at request offset `0x448`;
the supported preferences begin four bytes later. The PC/key-binding record
(`80803039`) begins at `0x6F8`. Its 60 packed key bindings use the existing native
action map; primary and secondary halves, modifier bits, and unbound sentinel
`0x74` are preserved.

`opcode701_schema.inl` captures the complete fixed descriptor tree, including
presence bits on array elements and signed integer biases. The decoder consumes
the whole request, including unrelated fields, but merges only settings. Omitted
fields retain their saved values. Truncated input, unexpected trailing bytes,
nonzero envelope extensions, and invalid setting values are refused without
changing memory or storage. Full native snapshots occupy 2,463 bytes.

The state writer checks account identity and the previous settings under the
account lock. It commits only the settings tables and account revision, then
publishes to memory. Identical updates do not write again. The response reports
success only after the commit. No account object is pushed back: the native client
already applies these preferences locally, and a full replacement could overwrite
unrelated client-owned account fields.

Schema 5 adds eight PC/display values. Existing settings, key bindings, characters,
inventory, and progression remain intact. The PC initialization version is saved
alongside FOV and the local/account key-binding selector so startup does not reseed
completed settings. Local graphics cvars remain owned by the game.

Verification:

- `account_settings_tests.vcxproj`: independent native wire fixtures, all byte
  truncations of a full snapshot, extra bytes, invalid values, partial updates,
  response capacity, duplicate saves, stale settings/identity, simulated SQLite
  failure, schema-4 migration, and a new-process reload followed by native record
  encoding.
- `persistence_tests.vcxproj`: existing persistence and migration regressions, plus character
  add/remove round trips: the removed character's durable rows, reuse of a freed key, and removing
  the last character.
- `opcode501_request_tests.vcxproj`: the create-character request (race, gender and class, decoded
  from retail captures) and the delete-character request (one 64-bit character id).
- `settings_dll_startup_tests.vcxproj`: loads the final release DLL and its matching
  private PDB, then exercises the production sign-in identity and character
  selection functions against an isolated copy of an existing save that holds at least one
  character. It also
  checks saved settings and inventory after reopening the database. Stage the
  DLL/PDB and a consistent SQLite backup in
  `build/unit/settings_dll_startup/fixture` (database under `Dawn/player-state.db`),
  then pass the staged DLL path and settings JSON path to the test executable.
  The test refuses DLL paths outside that fixture layout and does not install
  hooks or start the game.
- `python tools/testing/settings_wire_fixtures.py <destiny2_unpacked.bin>`: checks
  generated descriptors and wire fixtures against the pinned native image. Add
  `--write` only to regenerate them. The image SHA-256 is checked before reading
  reflection metadata.

The pinned image SHA-256 is
`63d128f1c759b92d32b0f226bcbec828bc58cef193ee0df6fdd582bd0290ed1e`.

## Release build regression

The first settings DLL reused an old `state_account_runtime.obj` whose dependency
log lacked the account/settings headers. `set_primary_soid` reserved 287,792 bytes
for its account copy, but the newly compiled copy constructor needed 287,816.
The resulting stack-cookie failure (`0xC0000409`, fast-fail 2) occurred during
opcode 503 sign-in, before opcode 701. Separately compiled unit tests did not
exercise this mixed binary. The final-DLL startup test reproduces that failure
with the old artifact and passes after a full rebuild in an empty intermediate
directory. Use a fresh intermediate directory for release builds after shared
state layout changes, and run this artifact test before installing the result.
