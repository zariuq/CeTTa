(base)
!(collapse (import! &self support/import_foreign_pkg/module.metta))
!(collapse (match &self (base) (base)))
!(collapse (match &self (foreign-before) (foreign-before)))
!(collapse (match &self (foreign-after) (foreign-after)))
!(plugin-tag)
!(plugin-inc 4)
; The names a Python module registers go to the module that imports it, here
; `module.metta`; the program gets them when it imports the module itself.
!(import! &self support/import_foreign_pkg/plugin)
!(plugin-tag)
!(plugin-inc 4)
