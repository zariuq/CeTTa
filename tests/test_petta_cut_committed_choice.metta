(a b)
(a c)

(= (match-single-via-cut $space $pattern $outPattern)
   (let* (($x (match $space $pattern $outPattern))
          ($temp (cut)))
         $x))

(= (match-single-via-once $space $pattern $outPattern)
   (once (match $space $pattern $outPattern)))

!(collapse (match-single-via-once &self (a $x) (a $x)))
!(collapse (match-single-via-cut &self (a $x) (a $x)))
!(test (collapse (match-single-via-cut &self (a $x) (a $x))) ((a b)))

(foo 1)
(foo 2)

!(let $x (match-single-via-cut &self (foo $n) $n)
      (add-atom &self (bar $x)))

!(test (collapse (match &self (bar $n) (bar $n))) ((bar 1)))
