; Interpretation of MeTTaSExpressionPresentation.module (see the C-style forms
; file for the template encoding). A parenthesized list is the expression of
; its items; ! before an atom is resolved by pres:runs after reading.
(pres:version sexpr "1.0")

(pres:forms sexpr
  ((SProgram (cs:arg 0))
   (STopAtom (cs:arg 0))
   (SExpr (cs:arg 0))))

(pres:token-forms sexpr
  ((SLeaf word) (SText string) (SRawText raw)))

; Each top-level atom of the program is a statement (a variable scope);
; leaves are SAtom.
(pres:statement sexpr "STop" "\n")
(pres:leaf-category sexpr "SAtom")
