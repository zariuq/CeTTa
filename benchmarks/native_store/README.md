# Native byte-map and SQLite point-read comparison

`compare_sqlite.c` compares identical binary keys and values through captured
CeTTa CSI/delta and ACT views, and a prepared SQLite query. It builds a SQLite
`WITHOUT ROWID` primary-key table, uses `synchronous=FULL`, vacuums it, and
allows a 64 MiB SQLite page cache. Both reads copy the complete value and
compute the same checksum. Every engine gets the same pseudorandom lookup
sequence; measured rounds rotate their execution order after warming.

This is a native byte-map benchmark. It does not measure MeTTa matching,
joins, RPC, application indexes/receipts or durable per-operation write latency.
SQLite is an independent optional benchmark dependency, not a runtime backend.

Build with the desired SQLite headers/link flags, for example:

```sh
cc -std=c11 -O2 -Isrc -Ivendor/sqlite \
  benchmarks/native_store/compare_sqlite.c src/native_disk_store.c \
  src/native_disk_index.c src/native_journal.c src/native_sha256.c \
  src/native_act_base.c src/act_trie.c -lsqlite3 -o runtime/compare-native-sqlite
runtime/compare-native-sqlite runtime/new-byte-map-comparison 65536 262144 5
```

The directory must be new. Output is JSON with versions, dataset geometry,
per-round mean latency, build/compaction time and image byte counts. Each value
is 96 bytes; each key has a common 32-byte prefix and an 8-byte ordinal. Native
ACT image sizes describe the current base and active delta. The fixture also
pins the prior uncompacted delta for comparison; that old generation consumes
additional disk until its reader closes and collection occurs. SQLite size
is its vacuumed database. The fixture's heap/input and mapped residency are
not a production memory qualification.

At 65,536 records, five rotated rounds against SQLite 3.46.1 gave median mean
point-read times of 1.879 microseconds for ACT, 1.909 for SQLite, and 10.657 for
the uncompacted native delta. Treat ACT and SQLite as roughly tied on this
workload; the difference is too small to establish a robust speed advantage.
The ACT base plus empty delta occupied 6,693,772 bytes; vacuumed SQLite occupied
9,609,216 bytes; the ACT image was about 30% smaller. These results do not
establish parity for other database workloads or a different host/build.
