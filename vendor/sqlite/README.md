# SQLite amalgamation

SQLite 3.53.4, unmodified sqlite3.c and sqlite3.h. SQLite is in the public domain.

Source: https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
Archive SHA3-256: 628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e
License: https://www.sqlite.org/copyright.html

Only ENABLE_DURABLE=1 builds link SQLite. SQLITE_PROVIDER=system selects the
system development package instead. Updates replace both files from one official
release after checking its published digest, reviewing the release notes, and
running the durable store and recovery tests. Do not patch the amalgamation.
