; Minimal higher-order rule-space match:
; repeated type variables must be refined across multiple argument positions.

!(bind! &rb (new-space))
!(add-atom &rb (: mp (-> (→ $p $q) $p $q)))

!(assertAlphaEqualToResult
  (match &rb
     (: $f (-> (→ (→ $a (→ $b $c)) (→ (→ $a $b) (→ $a $c)))
                (→ $a (→ $b $a))
                $r))
     (: ($f ax2 ax1) $r))
  ((: (mp ax2 ax1) (→ (→ $x $y) (→ $x $x)))))
