!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self compact_import_cases_v1.metta)

!(bind! &mixed-kb (new-space))
!(bind! &mixed-interpreter (new-space))
!(add-atom &mixed-interpreter (= (fof $name axiom $f) (Axiom $name $f)))
!(add-atom &mixed-interpreter (= (cnf $name axiom $c) (Clause $name $c)))
!(import-test:run mixed &mixed-kb &mixed-interpreter)

!(bind! &tagged-kb (new-space))
!(bind! &tagged-interpreter (new-space))
!(add-atom &tagged-interpreter (= (fof $name axiom $f) (Axiom $name $f)))
!(add-atom &tagged-interpreter (= (cnf $name axiom $c) (Clause $name $c)))
!(import-test:run tagged &tagged-kb &tagged-interpreter)
