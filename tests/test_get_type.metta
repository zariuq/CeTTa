; get-type operation
(: x Number)
(: f (-> Number Number))
(: tuple-cap T0)
(: tuple-cap T1)
(: tuple-cap T2)
(: tuple-cap T3)
(: tuple-cap T4)
(: accept-last-tuple-type (-> (T4 T4 T4) BeyondFormerFrontier))
!(assertEqual (get-type get-type) (-> Atom %Undefined%))
!(assertEqual (get-type x) Number)
!(assertEqual (get-type 42) Number)
!(assertEqual (get-type "hello") String)
!(assertEqual (get-type f) (-> Number Number))
; (T4 T4 T4) is the last of 125 inferred tuple types. Applicability must not
; lose it at the retired 64-entry implementation frontier.
!(assertEqual
    (get-type (accept-last-tuple-type (tuple-cap tuple-cap tuple-cap)))
    BeyondFormerFrontier)
