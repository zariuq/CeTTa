(: pcg-inspect (-> Atom %Undefined%))
(= (pcg-inspect $x) (get-type $x))
!(pcg-inspect (a 6))
!(pcg-inspect ())

(: pcg-required-list (-> (%Undefined% %Undefined%) Atom))
(= (pcg-required-list $x) OK)
(: pcg-pass-data (-> Atom %Undefined%))
(= (pcg-pass-data $x) (pcg-required-list $x))
!(collapse (pcg-pass-data (a 6)))
!(collapse (pcg-pass-data (a 6 7)))

(: pcg-constructor (-> Number String))
!(pcg-inspect (pcg-constructor 6))
!(pcg-inspect (pcg-constructor true))

; The callable-looking held value must not execute while being inspected.
!(: pcg-ignore Number)
!(pcg-inspect (println! SHOULD-NOT-RUN))
