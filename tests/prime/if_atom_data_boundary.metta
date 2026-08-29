; A selected control branch may return syntax through an Atom-typed field.
; Full observation must preserve that field as data, while evaluating the same
; ill-typed application directly must still reject it.

(: Test.Rel (-> Type Type Type))
(: Test.List (-> Type Type))
(: Test.Wrap (-> Atom Atom))
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
