; HE reads %Undefined% as any type at the top of a formal only: (List
; %Undefined%) rejects (List String).
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: g (-> (List Number) Number))
(= (g Nil) 0)
(= (g (Cons $h $t)) 1)
(: f (-> (List %Undefined%) Number))
(= (f $l) (g $l))
!(f (Cons "x" Nil))
