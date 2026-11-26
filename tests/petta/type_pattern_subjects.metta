; get-type answers the declarations whose subject the value instantiates, as
; SWI-PeTTa's get-type matches (: X T) against the space: a ground value is
; typed by a pattern subject, its type taken under that instance.  A typed
; call's guards read the same relation, so two arguments that meet one
; declaration jointly bind the caller's variable (stable-fixed e4e345f,
; typed_declarations_once assertion 26).
(: (wrap $x) (Wrap $x))
(: (wrap good) GoodWrap)
(: (pair $a $a) Same)

; Positive: a pattern subject and a written one both type a ground value.
!(collapse (get-type (wrap good)))
!(collapse (get-type (wrap bad)))
!(collapse (get-type (pair 1 1)))
; Negative: a value the pattern does not cover keeps its tuple type alone.
!(collapse (get-type (pair 1 2)))
!(collapse (get-type wrap))

; A declaration's shared type variable meets both arguments' types.
(: same-wrap (-> (Wrap $t) (Wrap $t) Number))
(: same-wrap (-> String String Number))
(= (same-wrap $a $b) 1)
!(collapse (let $x $free (let $r (same-wrap (wrap $x) (wrap good)) $x)))
!(collapse (let $x $free (let $r (same-wrap (wrap $x) (wrap good)) (let $x bad $r))))
!(collapse (same-wrap (wrap good) (wrap good)))
; Negative: the shared variable cannot be good and bad at once.
!(collapse (same-wrap (wrap good) (wrap bad)))
(: wrap-good (-> (Wrap good) Number))
(= (wrap-good $w) 2)
!(collapse (wrap-good (wrap good)))
!(collapse (wrap-good (wrap bad)))
