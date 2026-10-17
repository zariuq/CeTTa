!(import! &self "../lib/lib_module.metta")

; Commands and invalid Unicode scalars do not become declarations.
!(module:read-text ModulePortableV1 "command" "!(module:document LNil)")
!(module:read-text ModulePortableV1 "surrogate"
   "(module:document (module:codepoints (LCons 55296 LNil)))")
