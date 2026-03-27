; Re-registering the same namespace should replace the mount target cleanly.
!(register-module! support/import_pkg)
!(register-module! support_alt/import_pkg)
!(import! &alt import_pkg:moduleA)
!(assertEqualToResult (match &alt (pkg-root $x) $x) (alt-loaded))
!(assertEqualToResult (match &alt (pkg-helper $x) $x) ())
