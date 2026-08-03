; Intrinsic get-type answers precede authored relation extensions, while
; downstream failure may backtrack from the intrinsic Number answer to the
; explicit EvenNumber clause.
(= (get-type 2) EvenNumber)
(: accepts-even (-> EvenNumber Symbol))
(= (accepts-even $value) accepted)

!(collapse (get-type 2))
!(accepts-even 2)

; A typed higher-order call must validate the callable argument before either
; evaluating the call or reporting a result type.  `not` has no PeTTa type
; declaration here, so both collapsed computations are empty.
(: apply (-> (-> $tx $ty) $tx $ty))
(= (apply $f $x) ($f $x))

!(collapse (apply not False))
!(collapse (get-type (apply not False)))

; The parametric result variable remains relational when the declaration
; itself is inspected.
!(let (get-type apply)
      (-> (-> Bool Bool) Bool $result)
      $result)
