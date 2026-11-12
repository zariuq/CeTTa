; Judgments that separate HE's n-ary arrows from curried arrows.  HE's arrow `(-> P1 ... Pn R)`
; takes exactly n arguments: a partial application, an over-application and an arrow that
; matches an arrow parameter of another arity have no type.  Under a curried reading each of
; them would be typed.

(: sA SA)
(: sB SB)
(: sk (-> SA (-> SB SC)))
(: sg (-> SA SB SC))
(: sapply (-> (-> $a $b) $a $b))
(: scurry-a (-> (-> $a $b $c) $a (-> $b $c)))

; The flat arity test.
!(get-type (+ 1))
!(get-type (sg sA))
!(get-type ((sg sA) sB))
!(get-type (sk sA sB))
!(get-type ((sk sA) sB))
!(get-type (scurry-a + 2 3))
; An arrow-valued scheme variable matches only an arrow of the same arity.
!(get-type (sapply sg sA))
!(get-type (sapply (sk sA) sB))
