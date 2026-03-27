; HE-style module imports: register a module root, import into a fresh &name
; or &self, and reuse the same imported file without duplicating atoms.
!(register-module! support/import_pkg)
!(import! &db import_pkg:moduleA)
!(assertEqualToResult (match &db (pkg-root $x) $x) (loaded))
!(assertEqualToResult (match &db (pkg-helper $x) $x) (loaded))
!(import! &facts import_pkg:data:Facts)
!(assertEqualToResult (match &facts (item $x) $x) (alpha beta))
!(import! &self import_pkg:moduleA)
!(import! &self import_pkg:moduleA)
!(assertEqualToResult (match &self (pkg-root $x) $x) (loaded))
!(assertEqualToResult (match &self (pkg-helper $x) $x) (loaded))
