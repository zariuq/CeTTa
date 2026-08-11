!(import! &self langdef)
!(import! &self system)

(= (test:metamath-positive $parsed)
   (case $parsed
     (((LangDef.ParseAccepted MetamathV1 $digest
         ((mm-declare-constants
            (mm-symbol "wff")
            (mm-symbol "|-")))) True)
      ($_ False))))

(= (test:metamath-rejected $parsed)
   (case $parsed
     (((LangDef.ParseRejected MetamathV1 $digest $byte $expected) True)
      ($_ False))))

(= (test:metamath-parse-accepted $parsed)
   (case $parsed
     (((LangDef.ParseAccepted MetamathV1 $digest $constructs) True)
      ($_ False))))

(= (test:metamath-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 1) True)
      ($_ False))))

(= (test:metamath-declarations-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 2) True)
      ($_ False))))

(= (test:metamath-three-declarations-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 3) True)
      ($_ False))))

(= (test:metamath-seven-declarations-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 7) True)
      ($_ False))))

(= (test:metamath-eight-declarations-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 8) True)
      ($_ False))))

(= (test:metamath-six-declarations-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 6) True)
      ($_ False))))

(= (test:metamath-five-declarations-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 5) True)
      ($_ False))))

(= (test:metamath-ten-declarations-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 10) True)
      ($_ False))))

(= (test:metamath-twenty-declarations-imported $result)
   (case $result
     (((LangDef.Imported MetamathV1 $digest 20) True)
      ($_ False))))

(= (test:metamath-stage-rejected $result)
   (case $result
     (((LangDef.StageRejected MetamathV1 $digest $stage $detail) True)
      ($_ False))))

(= (test:metamath-stage-accepted-incomplete $result)
   (case $result
     (((LangDef.StageAcceptedIncomplete
          MetamathV1 $digest $stage $detail) True)
      ($_ False))))

!(bind! &metamath-langdef-manifest
   (system:getenv-or-default
     "CETTA_LANGDEF_TEST_MANIFEST"
     "langdef/metamath/langdef.metta"))
!(bind! &metamath-langdef
   (langdef:load &metamath-langdef-manifest))

!(assertEqual
   (test:metamath-positive
     (langdef:parse-file &metamath-langdef
       "tests/langdef/metamath/positive_constants.mm"))
   True)
!(assertEqual
   (test:metamath-parse-accepted
     (langdef:parse-file &metamath-langdef
       "tests/langdef/metamath/syntax_every_form.mm"))
   True)
!(assertEqual
   (test:metamath-positive
     (langdef:parse-text &metamath-langdef "$c wff |- $. "))
   True)

!(bind! &before-rejected (count-atoms &self))
!(assertEqual
   (test:metamath-rejected
     (langdef:parse-file &metamath-langdef
       "tests/langdef/metamath/invalid_missing_terminator.mm"))
   True)
!(assertEqual (count-atoms &self) &before-rejected)

!(bind! &before-rejected-import (count-atoms &self))
!(assertEqual
   (test:metamath-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_unclosed_comment.mm"))
   True)
!(assertEqual (count-atoms &self) &before-rejected-import)

!(bind! &metamath-import
   (langdef:import-file &metamath-langdef
     "tests/langdef/metamath/positive_constants.mm"))
!(assertEqual
   (test:metamath-imported &metamath-import)
   True)
!(assertEqual
   (match &self
     (mm-declare-constants
       (mm-symbol "wff")
       (mm-symbol "|-"))
     True)
   True)

!(assertEqual
   (test:metamath-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_constants_variables.mm"))
   True)
!(assertEqual
   (match &self
     (mm-declare-variables
       (mm-symbol "ph")
       (mm-symbol "ps"))
     True)
   True)

!(assertEqual
   (test:metamath-three-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_disjoint_variables.mm"))
   True)
!(assertEqual
   (match &self
     (mm-declare-disjoint
       (mm-symbol "x")
       (mm-symbol "y")
       (mm-symbol "z"))
     True)
   True)
!(assertEqual
   (match &self
     (mm-declare-disjoint
       (mm-symbol "z")
       (mm-symbol "y"))
     True)
   True)

!(assertEqual
   (test:metamath-three-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_floating.mm"))
   True)
!(assertEqual
   (match &self
     (mm-declare-floating
       (mm-label "wph")
       (mm-symbol "wff")
       (mm-symbol "ph"))
     True)
   True)
!(assertEqual
   (test:metamath-seven-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_scoped_floating_reactivation.mm"))
   True)
!(assertEqual
   (match &self
     (mm-declare-floating
       (mm-label "wph-outer")
       (mm-symbol "wff")
       (mm-symbol "ph"))
     True)
   True)

!(assertEqual
   (test:metamath-five-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_essential.mm"))
   True)
!(assertEqual
   (match &self
     (mm-declare-essential
       (mm-label "hph")
       (mm-symbol "|-")
       (mm-symbol "ph"))
     True)
   True)
!(assertEqual
   (test:metamath-seven-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_axiom.mm"))
   True)
!(assertEqual
   (match &self
     (mm-declare-axiom
       (mm-label "ax-pair")
       (mm-symbol "|-")
       (mm-symbol "ph")
       (mm-symbol "ps"))
     True)
   True)

!(assertEqual
   (test:metamath-seven-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/support/metamath_mini_thm_v0.mm"))
   True)
!(assertEqual
   (match &self
     (mm-check-theorem-normal
       (mm-label "id")
       (mm-symbol "|-")
       (mm-symbol "P")
       (mm-proof-step "wp")
       (mm-proof-step "axp"))
     True)
   True)
!(assertEqual
   (test:metamath-twenty-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/support/metamath_demo0_theorem_v0.mm"))
   True)
!(assertEqual
   (test:metamath-six-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/support/metamath_mini_thm_compressed_v0.mm"))
   True)
!(assertEqual
   (test:metamath-six-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_theorem_normal_reuse.mm"))
   True)
!(assertEqual
   (test:metamath-eight-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_theorem_reused_essential.mm"))
   True)
!(assertEqual
   (test:metamath-six-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_theorem_compressed_reuse.mm"))
   True)
!(assertEqual
   (match &self
     (mm-check-theorem-compressed
       (mm-label "th1")
       (mm-symbol "wff")
       (mm-symbol "(")
       (mm-symbol "(")
       (mm-symbol "ph")
       (mm-symbol "->")
       (mm-symbol "ps")
       (mm-symbol ")")
       (mm-symbol "->")
       (mm-symbol "(")
       (mm-symbol "ph")
       (mm-symbol "->")
       (mm-symbol "ps")
       (mm-symbol ")")
       (mm-symbol ")")
       (mm-proof-step "wi")
       (mm-compressed-word "ABCZDC"))
     True)
   True)
!(assertEqual
   (test:metamath-ten-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_theorem_disjoint_substitution.mm"))
   True)
!(assertEqual
   (test:metamath-ten-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_theorem_compressed_disjoint_substitution.mm"))
   True)
!(assertEqual
   (match &self
     (mm-check-theorem-normal
       (mm-label "th")
       (mm-symbol "|-")
       (mm-symbol "a")
       (mm-symbol "b")
       (mm-proof-step "fa")
       (mm-proof-step "fb")
       (mm-proof-step "ax"))
     True)
   True)
!(bind! &before-proof-incomplete (count-atoms &self))
!(assertEqual
   (test:metamath-stage-accepted-incomplete
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_theorem_normal_unknown.mm"))
   True)
!(assertEqual
   (test:metamath-stage-accepted-incomplete
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_theorem_compressed_unknown.mm"))
   True)
!(assertEqual (count-atoms &self) &before-proof-incomplete)

!(assertEqual
   (test:metamath-eight-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/positive_scoped_floating_retype.mm"))
   True)

!(bind! &before-state-rejected (count-atoms &self))
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_inner_constant.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_symbol_kind_conflict.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_duplicate_active_variable.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_inactive_disjoint_variable.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_duplicate_disjoint_variable.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_floating_typecode.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_floating_inactive_variable.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_duplicate_active_floating.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_floating_label_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_duplicate_floating_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_symbol_after_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_essential_typecode.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_essential_undeclared_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_essential_variable_without_float.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_essential_inactive_float.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_cross_kind_duplicate_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_axiom_typecode.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_axiom_undeclared_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_axiom_variable_without_float.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_axiom_inactive_float.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_duplicate_axiom_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_axiom_label_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_variable_after_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_essential_label_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_duplicate_essential_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_symbol_after_essential_label.mm"))
   True)
!(assertEqual (count-atoms &self) &before-state-rejected)

!(bind! &before-proof-rejected (count-atoms &self))
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_missing_disjoint.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_wrong_claim.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_unknown_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_self_reference.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_inactive_hypothesis.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_compressed_out_of_range.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_reused_missing_disjoint.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_reused_missing_essential.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/support/metamath_demo0_theorem_noblock_v0.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_normal_label_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_compressed_label_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_normal_duplicate_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_compressed_duplicate_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_normal_variable_typecode.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_compressed_variable_typecode.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_normal_undeclared_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_compressed_undeclared_symbol.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_normal_reused_missing_essential_variable.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_compressed_reused_missing_essential_variable.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_theorem_compressed_reused_missing_disjoint.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_symbol_after_normal_theorem_label.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/invalid_symbol_after_compressed_theorem_label.mm"))
   True)
!(assertEqual (count-atoms &self) &before-proof-rejected)

!(bind! &scoped-import
   (langdef:import-file &metamath-langdef
     "tests/langdef/metamath/positive_scoped_reactivation.mm"))
!(assertEqual
   (case &scoped-import
     (((LangDef.Imported MetamathV1 $digest 5) True)
      ($_ False)))
   True)

!(assertEqual
   (test:metamath-five-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/include/basic_root.mm"))
   True)
!(assertEqual
   (match &self
     (mm-declare-axiom
       (mm-label "iax")
       (mm-symbol "|-")
       (mm-symbol "ip"))
     True)
   True)
!(assertEqual
   (match &self
     (mm-check-theorem-normal
       (mm-label "ith")
       (mm-symbol "|-")
       (mm-symbol "ip")
       (mm-proof-step "iwp")
       (mm-proof-step "iax"))
     True)
   True)

!(assertEqual
   (test:metamath-six-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/include/nested_root.mm"))
   True)
!(assertEqual
   (match &self
     (mm-check-theorem-normal
       (mm-label "nmid")
       (mm-symbol "|-")
       (mm-symbol "np")
       (mm-proof-step "nwp")
       (mm-proof-step "nax"))
     True)
   True)
!(assertEqual
   (match &self
     (mm-check-theorem-normal
       (mm-label "nroot")
       (mm-symbol "|-")
       (mm-symbol "np")
       (mm-proof-step "nwp")
       (mm-proof-step "nax"))
     True)
   True)

!(assertEqual
   (test:metamath-five-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/include/duplicate_root.mm"))
   True)
!(assertEqual
   (test:metamath-five-declarations-imported
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/include/self_root.mm"))
   True)
!(assertEqual
   (match &self
     (mm-check-theorem-normal
       (mm-label "sth")
       (mm-symbol "|-")
       (mm-symbol "sp")
       (mm-proof-step "swp")
       (mm-proof-step "sax"))
     True)
   True)

!(bind! &before-include-rejected (count-atoms &self))
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/include/invalid_child_root.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/include/missing_child_root.mm"))
   True)
!(assertEqual
   (test:metamath-stage-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/include/incomplete_child_root.mm"))
   True)
!(assertEqual
   (test:metamath-rejected
     (langdef:import-file &metamath-langdef
       "tests/langdef/metamath/include/inner_scope_root.mm"))
   True)
!(assertEqual (count-atoms &self) &before-include-rejected)

!(assertEqual (langdef:close &metamath-langdef) True)
