; A value of a typed constructor is its own: a shared value is evaluated
; once per distinct node, not once per path to it.
(: Tree Type)
(: leaf (-> Number Tree))
(: node (-> Tree Tree Tree))
(= (tdag $n) (if (== $n 0) (leaf 1) (let $t (tdag (- $n 1)) (node $t $t))))
!(let $d (tdag 60) ok)
!(let $d (tdag 60) (get-type $d))
!(== (tdag 60) (tdag 60))
!(let $d (tdag 3) $d)

; So is a value of untyped constructors.
(= (dag $n $leaf) (if (== $n 0) $leaf (let $t (dag (- $n 1) $leaf) (pair $t $t))))
!(== (dag 60 1) (dag 60 1.0))
!(== (dag 60 1) (dag 60 2))

; A shared value passes through calls, spaces, states and comparisons
; once per distinct node.
(= (id2 $x) $x)
!(let $d (dag 60 1) (let $u (add-atom &self (holds $d)) (match &self (holds $x) ok)))
!(let $d (dag 60 1) (size-atom (collapse (unique (superpose ($d $d))))))
!(let $d (dag 60 1) (let $s (new-state $d) (let $v (get-state $s) ok)))
!(let $d (dag 60 1) (let $e (id2 $d) ok))
!(let $d (dag 60 1) (if (== $d $d) yes no))
!(let $d (tdag 60) (let $u (add-atom &self (stored $d)) (match &self (stored $v) (== $v $d))))
!(let $d (tdag 60) (let $e (id2 $d) ok))
!(let $d (tdag 60) (case $d (((node $a $b) yes))))
!(let $d (dag 60 1) (let $u (add-atom &self (kept $d)) (let $r (remove-atom &self (kept $d)) (collapse (match &self (kept $k) k)))))

; An argument that is a call runs each time its value is asked for.
(: Wrap (-> Number Wrap))
(= (tick) (let $u (add-atom &self tick) 1))
!(Wrap (tick))
!(Wrap (tick))
!(let $w (Wrap (tick)) (let $v $w $v))
!(collapse (match &self tick tick))

; A call held by an Atom parameter is not run, however often its holder is.
(: Hold (-> Atom Hold))
(= (tock) (let $u (add-atom &self tock) 1))
!(let $h (Hold (tock)) (let $v $h (let $w $h $w)))
!(collapse (match &self tock tock))

; An equation added after a value was computed applies to it.
(: Pt (-> Number Number Point))
!(let $p (Pt 1 2) (let $u (add-atom &self (= (Pt 1 2) reduced)) $p))
!(Pt 1 2)
(= (mk-later) (later-node a a))
!(mk-later)
!(add-atom &self (= (later-node a a) reduced))
!(mk-later)

; So does a declaration that no longer admits its arguments.
(: Q (-> Number Q))
!(collapse (let $q (Q 1) (let $u (remove-atom &self (: Q (-> Number Q))) (let $v (add-atom &self (: Q (-> String Q))) $q))))

; Equal values stay separate answers.
(: Pair2 (-> Number Number Pair2))
(= (choose) (Pair2 1 2))
(= (choose) (Pair2 1 2))
!(collapse (choose))
!(collapse (superpose ((Pair2 1 2) (Pair2 1 2))))
!(collapse (let $p (Pair2 1 2) (superpose ($p $p))))

; A call that stays unreduced answers once per equation that leaves it so.
(: Fr (-> Number Fr))
(= (Fr $x) NotReducible)
(= (Fr $x) NotReducible)
!(collapse (Fr 1))
!(collapse (let $f (Fr 1) $f))

; A value under an expected type other than its own is checked.
(: Other Type)
(: want-other (-> Other Other))
(= (want-other $x) $x)
!(collapse (want-other (leaf 1)))
!(collapse (let $l (leaf 1) (want-other $l)))
!(collapse (let $l (leaf 1) (let $u (add-atom &self (: leaf (-> Number Other))) (want-other $l))))

; Substitution reaches a value computed before its variables were bound.
!(let $a (+ $b 1) (let $b 1 $a))
!(let* (($a (+ $b 1)) ($b 1)) $a)
!(let $a (+ $b 1) (let $b 1 (+ $a 10)))
!(let $a (+ $b 1) (let $b 1 (if (== $a 2) yes no)))
!(let $a (+ $b 1) (let $b 1 (pair $a)))
!(collapse (let $a (+ $b 1) (let $b (superpose (1 1 2)) (pair $a))))
!(let $a (quote (+ $b 1)) (let $b 1 $a))
!(collapse (let* (($a (+ $b 1)) ($b "s")) $a))
