; transform reads a space with an imported module as match does: the
; space's own rows, then the module's.
!(import! &self (library lib_transform))
!(import! &self index_d5_module)
(p 1)
(p 2)
!(d5 (collapse (transform &self (p $x) (got $x))))
