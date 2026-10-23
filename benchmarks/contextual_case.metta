; Source branch bodies retain their frame until selected. Increase the depth
; to repeat the same ordered case operation with fresh RHS-local variables.
(: contextual-case-length (-> Atom %Undefined%))
(= (contextual-case-length $list)
   (case $list
     ((End 0)
      ((Link $head $tail) (+ 1 (contextual-case-length $tail))))))
!(contextual-case-length (Link a (Link b (Link c End))))
