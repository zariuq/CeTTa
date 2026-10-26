; Type ascriptions constrain logical bindings without changing term identity.
!(test
   (let $typed (the (List Number) ($x))
        (let $x $alias
             (let 7 $alias $typed)))
   (7))

!(test
   (let $typed (the (List Number) ($x))
        (=alpha $typed ($fresh)))
   True)

; A refuted binding is a catchable Error, including through a variable alias.
!(test
   (let $typed (the (List Number) ($x))
        (let $x $alias
             (car-atom
               (catch (let "bad" $alias $typed)))))
   Error)

; Backtracking restores both bindings and their attached obligations.
!(test
   (collapse
     (let $typed (the (List Number) ($x))
          (let (superpose (7 8)) $x $typed)))
   ((7) (8)))

; The formal admits Error as data before its argument is evaluated.
; A postcondition alone is too late to establish this interpretation.
(: preparation-error-data (-> (| Number (Error $detail $context)) Number))
(= (preparation-error-data $value) 7)
!(preparation-error-data (Error payload context))
