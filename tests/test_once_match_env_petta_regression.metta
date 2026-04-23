(foo 1)
(foo 2)

(= (match-single $space $pat $ret)
   (once (match $space $pat $ret)))

!(test (match-single &self (foo $x) $x)
       1)

!(let $x (match-single &self (foo $y) $y)
   (add-atom &self (bar $x)))

!(test (collapse (match &self (bar $z) (bar $z)))
       ((bar 1)))
