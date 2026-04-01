!(import! &self mork)

!(bind! &src (new-space hash))
!(space-set-match-backend! &src pathmap-imported)
!(add-atom &src (edge a b))
!(add-atom &src (edge a b))
!(add-atom &src (edge b c))
!(assertEqual
  (mork:dump! &src "../runtime/test_import_act_module_surface.act")
  ())

!(import! &compiled "../runtime/test_import_act_module_surface.act")
!(assertEqualToResult
  (get-type &compiled)
  ((Space mork)))
!(assertEqualToResult
  (size &compiled)
  (3))
!(assertEqualToResult
  (match &compiled (edge a b) hit)
  (hit hit))

(base)
!(import! &self "../runtime/test_import_act_module_surface.act")
!(assertEqualToResult
  (match &self (base) hit)
  (hit))
!(assertEqualToResult
  (match &self (edge a b) hit)
  (hit hit))

!(bind! &mod (mod-space! "../runtime/test_import_act_module_surface.act"))
!(assertEqualToResult
  (get-type &mod)
  ((Space mork)))
!(assertEqualToResult
  (size &mod)
  (3))
!(assertEqualToResult
  (match &mod (edge b c) hit)
  (hit))
