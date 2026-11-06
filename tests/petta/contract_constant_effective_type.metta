;; A function parameter that every call passes the same name is a constant
;; of its call's version only when the name's effective type meets the
;; parameter: the type a guard reads, not an authored row it outranks.

;; Positive: a declared function passed at every call.
(: inc (-> Number Number))
(= (inc $x) (+ $x 1))
(: cc-apply (-> (-> $a Number) Number Number))
(= (cc-apply $f $n) (if (== $n 0) 0 (cc-apply inc (- $n 1))))
(: cc-apply-outer (-> Number Number))
(= (cc-apply-outer $n) (cc-apply inc $n))
!(collapse (cc-apply-outer 2))
!(collapse (cc-apply inc 2))

;; Negative: a truth value is Bool, whatever row is written for it.
(: true (-> $z Number))
(: cc-inner (-> (-> $a Number) Number Number))
(= (cc-inner $f $n) (if (== $n 0) 0 (cc-inner true (- $n 1))))
(: cc-outer (-> Number Number))
(= (cc-outer $n) (cc-inner true $n))
!(get-type true)
!(collapse (cc-outer 2))
!(collapse (cc-inner true 2))

;; Negative: a value argument typed by its effective type, not a row.
(: false Number)
(: cc-count (-> Number Number Number))
(= (cc-count $x $n) (if (== $n 0) 0 (cc-count false (- $n 1))))
(: cc-count-outer (-> Number Number))
(= (cc-count-outer $n) (cc-count false $n))
!(collapse (cc-count-outer 2))
