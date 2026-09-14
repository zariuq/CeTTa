; Nested observations share revision-checked source arities. An equation
; added or removed between children must still change under-application.
; Sharing derived storage must also preserve duplicate answer occurrences.
(= (arity-changing $a $b $c) (three $a $b $c))
(= (arity-changing-sequence)
   (let* (($before (collapse (arity-changing a b)))
          ($_ (add-atom &self (= (arity-changing $a $b) (two $a $b))))
          ($after (collapse (arity-changing a b)))
          ($_ (add-atom &self (= (arity-changing $a $b) (two $a $b))))
          ($duplicates (collapse (arity-changing a b)))
          ($_ (remove-atom &self (= (arity-changing $a $b) (two $a $b))))
          ($removed (collapse (arity-changing a b))))
         (before $before after $after duplicates $duplicates removed $removed)))
!(arity-changing-sequence)
