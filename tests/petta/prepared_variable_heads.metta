; Folds whose step builds an expression with a variable head: the value the
; head holds when the step runs decides whether the expression is data or a
; call, and a call runs with its effects, once.
; A number at the head: data.
(= (best ($a $b) $x) (if (> $x $a) ($x $b) ($a (+ $b 1))))
!(foldl-atom (1 5 2 7 3 3) (0 0) $acc $x (best $acc $x))
(= (wrap $acc $x) ($x $acc))
!(foldl-atom (1 2 3) 0 $acc $x (wrap $acc $x))
; A symbol no equation defines: data.
(= (grow ($h $v) $x) ($h (+ $v $x)))
!(foldl-atom (1 2 4) (qq 0) $acc $x (grow $acc $x))
; A function without effects: called.
(= (double $y) (* $y 2))
(= (call-with $acc $f) ($f $acc))
(fn double)
(fn double)
!(foldl-atom (collapse (match &self (fn $f) $f)) 1 $acc $f (call-with $acc $f))
; A function with an effect: called, its effect made once per call.
(= (note $y) (let $_ (add-atom &self (noted $y)) (+ $y 1)))
(efn note)
(efn note)
!(foldl-atom (collapse (match &self (efn $f) $f)) 7 $acc $f (call-with $acc $f))
!(sort-atom (collapse (match &self (noted $y) $y)))
; The most frequent of a sorted list, the smallest on ties.
(= (mode-step ($c $cn $b $bn) $x)
   (if (== $x $c) ($c (+ $cn 1) $b $bn)
       (if (> $cn $bn) ($x 1 $c $cn) ($x 1 $b $bn))))
(= (mode $sorted)
   (let ($c $cn $b $bn) (foldl-atom $sorted (none 0 none 0) $acc $x (mode-step $acc $x))
     (if (> $cn $bn) $c $b)))
!(mode (1 1 2 2 2 3))
!(mode (1 1 2 2 3 3))
!(mode (4))
