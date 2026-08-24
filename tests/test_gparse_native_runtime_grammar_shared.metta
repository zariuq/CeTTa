!(import! &self gparse)

; These grammars are values constructed at evaluation time.  They exercise
; the same public generalized parsers as file-backed grammars.
(= (runtime-plus-grammar)
   (mkL (
     (VarD x Atom)
     (Prod expr-atom Expr ((Hl Atom)))
     (Prod atom-x Atom ((Hl x)))
     (Prod expr-plus Expr ((Hl Atom) (Tm PLUS) (Hl Atom))))))

(= (runtime-atom-grammar)
   (mkL (
     (VarD x Atom)
     (Prod expr-atom Expr ((Hl Atom)))
     (Prod atom-x Atom ((Hl x))))))

(= (runtime-ambiguous-grammar)
   (mkL (
     (VarD x Expr)
     (Prod expr-plus Expr ((Hl Expr) (Tm PLUS) (Hl Expr))))))

!(assertEqual
  (gparse:gll-parse-shared
    (runtime-plus-grammar) Expr (mkL (x PLUS x)))
  (Unique
    (NodeC expr-plus Expr 0 3
      (Cons (LeafC x 0 1)
        (Cons (TokC PLUS 1)
          (Cons (LeafC x 2 3) Nil))))))

!(assertEqual
  (gparse:glr-parse-shared
    (runtime-plus-grammar) Expr (mkL (x PLUS x)))
  (gparse:gll-parse-shared
    (runtime-plus-grammar) Expr (mkL (x PLUS x))))

; The same token stream stops parsing when the runtime value lacks the rule.
!(assertEqual
  (gparse:gll-parse-shared
    (runtime-atom-grammar) Expr (mkL (x PLUS x)))
  NoParse)

!(assertEqual
  (gparse:glr-parse-shared
    (runtime-atom-grammar) Expr (mkL (x PLUS x)))
  NoParse)

!(assertEqual
  (gparse:gll-parse-shared
    (runtime-ambiguous-grammar) Expr (mkL (x PLUS x PLUS x)))
  Ambiguous)

!(assertEqual
  (gparse:glr-parse-shared
    (runtime-ambiguous-grammar) Expr (mkL (x PLUS x PLUS x)))
  Ambiguous)
