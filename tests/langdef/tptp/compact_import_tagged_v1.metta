!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self compact_import_cases_v1.metta)

; HE evaluates nested data passed through ordinary expressions, so the
; mixed records meet the rewrites of the cases file; tagged records have no
; head a rewrite can match.
!(bind! &tagged-kb (new-space))
!(bind! &tagged-interpreter (new-space))
!(add-atom &tagged-interpreter (= (fof $name axiom $f) (Axiom $name $f)))
!(add-atom &tagged-interpreter (= (cnf $name axiom $c) (Clause $name $c)))
!(import-test:run tagged &tagged-kb &tagged-interpreter)
