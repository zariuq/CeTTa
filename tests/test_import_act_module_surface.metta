!(import! &self mork)

!(bind! &src (mork:new-space hash))
!(mork:add-atom &src (edge a b))
!(mork:add-atom &src (edge a b))
!(mork:add-atom &src (edge b c))
!(assertEqual
  (mork:dump! &src "../runtime/test_import_act_module_surface.act")
  ())

!(import! &compiled "../runtime/test_import_act_module_surface.act")
!(assertEqualToResult
  (get-type &compiled)
  ((Space pathmap)))
!(assertEqualToResult
  (size &compiled)
  (2))
!(assertEqualToResult
  (match &compiled (edge a b) hit)
  (hit))

(base)
!(import! &self "../runtime/test_import_act_module_surface.act")
!(assertEqualToResult
  (match &self (base) hit)
  (hit))
!(assertEqualToResult
  (match &self (edge a b) hit)
  (hit))

!(bind! &mod (mod-space! "../runtime/test_import_act_module_surface.act"))
!(assertEqualToResult
  (get-type &mod)
  ((Space pathmap)))
!(assertEqualToResult
  (size &mod)
  (2))
!(assertEqualToResult
  (match &mod (edge b c) hit)
  (hit))
