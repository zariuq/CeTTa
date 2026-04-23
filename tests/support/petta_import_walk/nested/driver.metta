!(import! &self tests/support/petta_import_walk/lib/ancestor_lib)

(= (ancestor-import-value)
   (ancestor-lib-value))
