!(bind! &pow (py-atom math.pow))
!(assertEqualToResult (&pow 2 3) (8.0))

!(import! &self math)
!(assertEqualToResult (math.min 0.6 0.7) (0.6))
!(assertEqualToResult (math.clamp 0.0 1.0 1.2) (1.0))

!(import! &rel support/import_rel/root.metta)
!(assertEqualToResult (match &rel (rel-root loaded) ok) (ok))
!(assertEqualToResult (match &rel (rel-helper loaded) ok) (ok))

!(register-module! support/import_pkg)
!(bind! &mod (mod-space! import_pkg.moduleA))
!(assertEqualToResult (match &mod (pkg-root loaded) ok) (ok))
!(assertEqualToResult (match &mod (pkg-helper loaded) ok) (ok))
