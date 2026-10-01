; An annotation query may refine its subject, not just its returned type.
(: (pcg-box 1) Box)
(: pcg-observe (-> Atom %Undefined%))
(= (pcg-observe $x)
   (let $t (get-type (pcg-box $x)) (pair $x $t)))
!(collapse (pcg-observe $q))

; Function inference queries the ordinary, extensible type relation.
(= (get-type secret) Number)
(: pcg-ctor (-> Number String))
(: pcg-inspect (-> Atom %Undefined%))
(= (pcg-inspect $x) (get-type $x))
!(collapse (pcg-inspect (pcg-ctor secret)))
