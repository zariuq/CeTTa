; A selected control branch may return syntax through an Atom-typed field.
; Full observation must preserve that field as data, while evaluating the same
; ill-typed application directly must still reject it.

(: Test.Rel (-> Type Type Type))
(: Test.List (-> Type Type))
(: Test.Wrap (-> Atom Proc))
(: Test.Pair
   (-> (Test.Rel Number Number)
       (Test.Rel Number Number)
       (Test.Rel Number Number)))
(: test-list-rel (Test.Rel (Test.List Number) (Test.List Number)))

!(if True
     (Test.Wrap (Test.Pair test-list-rel test-list-rel))
     ())

!(if True
     (cons-atom
       (Test.Wrap (Test.Pair test-list-rel test-list-rel))
       ())
     ())

!(if False
     (Test.Pair test-list-rel test-list-rel)
     branch-not-selected)

!(Test.Pair test-list-rel test-list-rel)

; Selecting a data-valued branch preserves that value. Explicit activation
; and a written computation remain distinct from forwarding held syntax.
!(bind! &if-data-effects (new-space))
(= (if-data:tick) (let $_ (add-atom &if-data-effects ran) 9))
(: if-data:choose (-> Atom Atom))
(= (if-data:choose $data) (if False bad $data))
!(if-data:choose (+ 5 1))
!(if-data:choose (if-data:tick))
!(collapse (get-atoms &if-data-effects))
!(let $value (if-data:choose (if-data:tick)) (eval $value))
!(collapse (get-atoms &if-data-effects))
!(if True (if-data:tick) bad)
!(collapse (get-atoms &if-data-effects))
; Reading a held Boolean inspects its value; it does not activate an
; arbitrary held expression in the condition position.
!(let $guard (if-data:choose True) (if $guard yes no))
!(let $guard (if-data:choose False) (if $guard yes no))
!(let $guard (if-data:choose (if-data:tick)) (if $guard yes no))
!(collapse (get-atoms &if-data-effects))
