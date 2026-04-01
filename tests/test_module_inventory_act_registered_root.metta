!(import! &self mork)

!(bind! &src (new-space hash))
!(space-set-match-backend! &src pathmap-imported)
!(add-atom &src (edge a b))
!(add-atom &src (edge a b))
!(add-atom &src (edge b c))
!(assertEqual
  (mork:dump! &src "../runtime/test_module_inventory_act_registered_root.act")
  ())

!(register-module! ../runtime)
!(import! &compiled runtime:test_module_inventory_act_registered_root)
!(assertEqualToResult
  (get-type &compiled)
  ((Space mork)))
!(bind! &mod (mod-space! runtime:test_module_inventory_act_registered_root))
!(bind! &mods (module-inventory!))

!(assertEqualToResult
  (get-type &mod)
  ((Space mork)))
!(assertEqualToResult
  (size &mod)
  (3))
!(assertEqualToResult
  (match &mods
    (loaded-module-format "runtime:test_module_inventory_act_registered_root" mork-act none)
    ok)
  (ok))
!(assertEqualToResult
  (match &mods
    (loaded-module-storage "runtime:test_module_inventory_act_registered_root" attached-compiled)
    ok)
  (ok))
!(assertEqualToResult
  (match &mods
    (loaded-module-space "runtime:test_module_inventory_act_registered_root" $space registered-root)
  ok)
  (ok))
