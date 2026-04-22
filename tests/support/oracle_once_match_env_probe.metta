(foo 1)
(foo 2)

(= (match-single $space $pat $ret)
   (once (match $space $pat $ret)))

!(match-single &self (foo $x) $x)

!(let $x (match-single &self (foo $y) $y)
   $x)
