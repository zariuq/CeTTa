# tomlc17

tomlc17 release R261003 (TOML 1.1, a superset of 1.0): `src/tomlc17.c` and `src/tomlc17.h`. MIT license (LICENSE).

One local change, in `scan_literal`: bare keys are ASCII (`A-Za-z0-9_-`), as TOML 1.1.0 as released requires. The release still accepts the 1.1 draft's Unicode bare keys; that branch stays in the file, compiled out. toml-test's `invalid/key/special-character.toml` checks it.

Source: https://github.com/cktan/tomlc17, tag R261003, commit 8d3766ddafa6515417f4885ae6878ef69fac401e

lib/toml (`src/library_toml.c`) turns a parsed document into JSON text, with
UTF-8 checking switched on. Updates replace both files from one release tag
after reviewing its changelog and running `make test-toml-v1`, keeping the
local change while a release needs it. Make no other changes to the files.
