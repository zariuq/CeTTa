; CeTTa's list syntax as an extension of the PeTTa form reader.  [x y] is a list and
; [x y | rest] a list pattern: elements are separated by layout, as in an expression,
; and a bare | token before the last element marks the rest.  Composed over
; PeTTaFormReaderSyntaxV1, each rule replaces the base rule of the same name or adds one.
(gslt-presentation-v1 PeTTaFormListExtensionV1
  (signature)
  (equations)
  (rewrites
    (rule def-petta-list-token-boundary (head (definition petta-list-token-boundary (alt eof (class petta-list-token-boundary-scalar)))) (body))
    (rule def-petta-list-token-like (head (definition petta-list-token-like (left (alt (node list-variable (right (char (cp 36)) (plus (class petta-list-token-scalar)))) (alt (node list-dollar-symbol (char (cp 36))) (node list-token (alt (seq (char (cp 124)) (seq (class petta-list-token-scalar) (star (class petta-list-token-scalar)))) (seq (class petta-list-token-first-scalar) (star (class petta-list-token-scalar))))))) (peek (ref petta-list-token-boundary))))) (body))
    (rule def-petta-list-element (head (definition petta-list-element (alt (ref petta-string) (alt (ref petta-expression) (alt (ref petta-list) (ref petta-list-token-like)))))) (body))
    (rule def-petta-list-rest (head (definition petta-list-rest (node list-rest (right (left (char (cp 124)) (peek (ref petta-list-token-boundary))) (right (ref petta-skip) (left (ref petta-list-element) (ref petta-skip))))))) (body))
    (rule def-petta-list-items (head (definition petta-list-items (seq (left (ref petta-list-element) (ref petta-skip)) (seq (star (left (ref petta-list-element) (ref petta-skip))) (opt (ref petta-list-rest)))))) (body))
    (rule def-petta-list (head (definition petta-list (node list (right (char (cp 91)) (left (right (ref petta-skip) (opt (ref petta-list-items))) (char (cp 93))))))) (body))
    (rule def-petta-atom (head (definition petta-atom (alt (ref petta-string) (alt (ref petta-expression) (alt (ref petta-list) (ref petta-token-like)))))) (body))
  ))
