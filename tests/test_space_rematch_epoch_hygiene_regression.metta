; Native-space regression:
; a derived atom with previously freshened variables must remain alpha-distinct
; when stored and then matched again from the space.

(= (fs2 $rb (: $x1 $a1) (: $x2 $a2))
   (match $rb (: $f (-> $a1 $a2 $b)) (: ($f $x1 $x2) $b)))

(= (fs $rb $ss)
   (fs2 $rb
        (match $ss (: $x1 $a1) (: $x1 $a1))
        (match $ss (: $x2 $a2) (: $x2 $a2))))

(= (fs-add-to-ss $rb $ss)
   (let $ns (collapse (fs $rb $ss))
     (let $nss (superpose $ns)
       (add-atom $ss $nss))))

!(bind! &rb (new-space))
!(add-atom &rb (: ax-mp (-> (→ $𝜑 $𝜓) $𝜑 $𝜓)))

!(bind! &ss (new-space))
!(add-atom &ss (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))
!(fs-add-to-ss &rb &ss)

!(assertAlphaEqualToResult
  (get-atoms &ss)
  ((: ax-1 (→ $a (→ $b $a)))
   (: (ax-mp ax-1 ax-1) (→ $u (→ $v (→ $w $v))))))

!(assertAlphaEqualToResult
  (match &ss (: (ax-mp ax-1 ax-1) $t) $t)
  ((→ $u (→ $v (→ $w $v)))))
