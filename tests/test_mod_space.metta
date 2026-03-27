!(register-module! support/import_pkg)
!(bind! &m (mod-space! import_pkg:moduleA))
!(assertEqualToResult (match &m (pkg-root $x) $x) (loaded))
!(assertEqualToResult (match &m (pkg-helper $x) $x) (loaded))
