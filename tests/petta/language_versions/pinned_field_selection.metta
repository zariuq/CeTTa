; (C 0) does not cover every value C builds, so decode's call is not
; deterministic where its argument is any T.
(: C (-[det]-> Number T))
(: D (-[det]-> Number T))
(= (decode (C 0)) 0)
(= (decode (D $x)) 1)
(: bad-decode (-[det]-> T Number))
(= (bad-decode $value) (decode $value))
