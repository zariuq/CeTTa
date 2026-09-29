!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self compact_binding_cases_v1.metta)

; HE evaluates nested data that flows through ordinary expressions, so a
; mixed formula meets the user rewrites of the cases file; tagged formulas
; have no head a rewrite can match.
!(binding-test:all tagged)
