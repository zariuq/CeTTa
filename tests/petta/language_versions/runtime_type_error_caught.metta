; A value that does not have its declared type raises an ordinary error:
; catch returns it as a value, and the run goes on.
(: red Colour)
(: shade Shade)
(: tint (-> Colour Shade))
(= (tint $c) (foldl-atom (1) shade $acc $item $c))

!(test (let* (($call (quote (tint red)))
              ($caught (catch (eval $call))))
         (repr $caught))
       "(Error (literal_type_mismatch red Shade) typecheck)")
