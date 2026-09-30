; Imported declarations invalidate facts already used by a typed call.
(: pcg-imported (-> Number Atom))
(= (pcg-imported $x) $x)
!(collapse (pcg-imported (+ 2 4)))
!(import! &self type_policy_import_revision_lib)
!(collapse (pcg-imported (+ 2 4)))
!(remove-atom &self (: pcg-imported (-> Number Atom)))
!(collapse (pcg-imported (+ 2 4)))
