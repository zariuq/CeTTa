; CeTTa's list syntax as an extension of the HE reader.  [x y] is a list and
; [x y | rest] a list pattern: elements are separated by layout, as in an expression,
; and a bare | token before the last element marks the rest.  Composed over
; HEReaderSyntaxV1, each rule replaces the base rule of the same name or adds one.
(gslt-presentation-v1 HEReaderListExtensionV1
  (signature)
  (equations)
  (rewrites
    (rule def-he-list-boundary (head (definition he-list-boundary (alt eof (alt (class he-whitespace-scalar) (alt (char (cp 40)) (alt (char (cp 41)) (alt (char (cp 59)) (alt (char (cp 91)) (char (cp 93)))))))))) (body))
    (rule def-he-list-word (head (definition he-list-word (node list-word (left (alt (seq (char (cp 124)) (seq (class he-list-word-tail-scalar) (star (class he-list-word-tail-scalar)))) (seq (class he-list-word-start-scalar) (star (class he-list-word-tail-scalar)))) (peek (ref he-list-boundary)))))) (body))
    (rule def-he-list-variable (head (definition he-list-variable (node list-variable (left (right (char (cp 36)) (seq (class he-list-variable-tail-scalar) (star (class he-list-variable-tail-scalar)))) (peek (ref he-list-boundary)))))) (body))
    (rule def-he-list-element (head (definition he-list-element (alt (ref he-string) (alt (ref he-list-variable) (alt (ref he-expression) (alt (ref he-list) (ref he-list-word))))))) (body))
    (rule def-he-list-rest (head (definition he-list-rest (node list-rest (right (left (char (cp 124)) (peek (ref he-list-boundary))) (right (ref he-skip) (left (ref he-list-element) (ref he-skip))))))) (body))
    (rule def-he-list-items (head (definition he-list-items (seq (left (ref he-list-element) (ref he-skip)) (seq (star (left (ref he-list-element) (ref he-skip))) (opt (ref he-list-rest)))))) (body))
    (rule def-he-list (head (definition he-list (node list (right (char (cp 91)) (left (right (ref he-skip) (opt (ref he-list-items))) (char (cp 93))))))) (body))
    (rule def-he-atom (head (definition he-atom (alt (ref he-string) (alt (ref he-variable) (alt (ref he-expression) (alt (ref he-list) (ref he-word))))))) (body))
  ))
