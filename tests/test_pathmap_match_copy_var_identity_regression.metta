; PathMap bridge-byte ingress must preserve variable co-reference by VarId,
; not by presentation spelling. Copying several theorem schemas through match
; used to collapse ax-2's third variable into its first variable.

!(bind! &ab (new-space pathmap))
!(add-atom &ab (: ax-1 (→ $p (→ $q $p))))
!(add-atom &ab (: ax-2 (→ (→ $p (→ $q $r)) (→ (→ $p $q) (→ $p $r)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $p) (¬ $q)) (→ $q $p))))

!(bind! &ss (new-space pathmap))
!(match &ab (: $x $a) (add-atom-nodup &ss (: $x $a)))

!(assertAlphaEqualToResult
  (match &ss (: ax-2 $a) $a)
  ((→ (→ $p (→ $q $r)) (→ (→ $p $q) (→ $p $r)))))
