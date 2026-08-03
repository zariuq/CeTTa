; Parsing predeclares authored equation heads without installing their clauses.
; This imported body can therefore call a relation declared later in this
; document, matching PeTTa's two-pass source semantics.
!(import! &self fixtures/import_forward)

(= (defined-after-import 41) 42)

!(imported-forward-call 41)
