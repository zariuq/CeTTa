; Regression: native forward chaining must allow reusing the same stored
; theorem twice in one rule application. The term-sharing rematch rewrite
; broke this and lost self-join theorems at FC depth 3.

(= (depth) 2)

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

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

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))
!(add-atom &ab (: ax-2 (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))))

!(bind! &ss (new-space))
!(match &ab (: $x $a) (add-atom &ss (: $x $a)))
!(if (<= 1 (depth)) (fs-add-to-ss &rb &ss) (empty))
!(if (<= 2 (depth)) (fs-add-to-ss &rb &ss) (empty))

!(assertEqual
  (> (size-atom
       (collapse
         (match &ss
           (: (ax-mp (ax-mp (ax-mp ax-1 ax-1) ax-1)
                     (ax-mp (ax-mp ax-1 ax-1) ax-1))
              $a)
           $a)))
     0)
  True)
