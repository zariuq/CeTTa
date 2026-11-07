; A field step leaves the caller's more specific instance owed, which a
; build step after it fails.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: f (-> (List $t) (List $u) (List $t)))
(= (f $l $l) $l)
(= (f (Cons $h $x) Nil) (f $x W))
(= (f $l W) (f (Cons "s" $l) (Cons "s" $l)))
(: u (-> (List Number) %Undefined%))
(= (u $l) (f $l Nil))
!(u (Cons 1 Nil))
