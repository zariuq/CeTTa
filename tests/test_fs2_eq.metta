; Test: fs2 called through equation evaluation
; This is the EXACT pattern from the forward chainer

!(bind! &rb (new-space))
!(add-atom &rb (: mp (-> (→ $p $q) $p $q)))

!(bind! &ss (new-space))
!(add-atom &ss (: ax1 (→ $a (→ $b $a))))
!(add-atom &ss (: ax2 (→ (→ $a (→ $b $c)) (→ (→ $a $b) (→ $a $c)))))

; fs2 as equation
(= (fs2 $rb (: $x1 $a1) (: $x2 $a2))
   (match $rb (: $f (-> $a1 $a2 $b)) (: ($f $x1 $x2) $b)))

; fs: get premises from &ss, apply fs2
(= (fs $rb $ss)
   (fs2 $rb
        (match $ss (: $x1 $t1) (: $x1 $t1))
        (match $ss (: $x2 $t2) (: $x2 $t2))))

; This is the minimal forward-chaining completeness regression:
; higher-order type constraints from ax2 must re-unify through the rule base.
!(assertAlphaEqualToResult
  (fs &rb &ss)
  ((: (mp ax1 ax1) (→ $u (→ $a (→ $b $a))))
   (: (mp ax1 ax2) (→ $u (→ (→ $a (→ $b $c)) (→ (→ $a $b) (→ $a $c)))))
   (: (mp ax2 ax1) (→ (→ $x $y) (→ $x $x)))
   (: (mp ax2 ax2) (→ (→ (→ $x (→ $y $z)) (→ $x $y))
                       (→ (→ $x (→ $y $z)) (→ $x $z))))))
