; Curried arrows turned on and off within a program: each call reads its
; head's types as the setting stands when it runs.
(: plus (-> Number Number Number))
(= (plus $x $y) (+ $x $y))
!(collapse (plus 1))
!(collapse (get-type (plus 1)))
!(pragma! curried-arrows on)
!(collapse (plus 1))
!(collapse (get-type (plus 1)))
!(collapse ((plus 1) 2))
!(pragma! curried-arrows off)
!(collapse (plus 1))
!(collapse (get-type (plus 1)))
