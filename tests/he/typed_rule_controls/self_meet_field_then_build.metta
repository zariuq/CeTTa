; A field step leaves the caller's more specific instance owed, which a
; build step after it fails.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: f (-> Number (List $t) (List $t)))
(= (f 0 $l) $l)
(= (f 1 (Cons $h $x)) (f 2 $x))
(= (f 2 $l) (f 0 (Cons "s" $l)))
(: u (-> (List Number) %Undefined%))
(= (u $l) (f 1 $l))
!(u (Cons 1 Nil))
