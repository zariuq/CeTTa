; Distinct closures share a constructor prefix but capture different offsets.
; The answer depends on each capture. Pass a longer list to selector-lookup
; to scale the number of specialization records and ordinary route lookups.
(= (selector-offset (Payload $padding $offset) $value) (+ $offset $value))
(= (selector-apply $callable $value) ($callable $value))
(= (selector-lookup $offsets)
   (foldl-atom $offsets 0 $sum $offset
     (+ $sum (selector-apply
       (selector-offset
         (Payload (a b c d e f g h i j k l m n o p q r s t u v w x y z) $offset))
       0))))
!(selector-lookup (0 1 2 3 4 5 6 7))
