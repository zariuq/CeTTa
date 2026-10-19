!(import! &self "../lib/lib_module.metta")

!(let*
  (($original (module:read-file ModulePortableV1 "same" "tests/module_portable/complete.module"))
   ($encoded (module:write-text ModulePortableV1 $original))
   ($restored (module:read-text ModulePortableV1 "same" $encoded))
   ($reencoded (module:write-text ModulePortableV1 $restored)))
  (== $encoded $reencoded))
