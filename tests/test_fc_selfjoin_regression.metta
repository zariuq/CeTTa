; Regression: forward chaining depth 2 must allow reusing the same stored
; theorem twice in one rule application. The older collapse-based accumulation
; probe was not a stable correctness witness; use the snapshot/add lane and
; the exact depth-2 closure count instead.

(= (fs-add-to-ss $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: $x1 $a1)
       (match $ss0 (: $x2 $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (add-atom $ss (: ($f $x1 $x2) $b)))))))

(= (quiet-copy-space $src $dst)
   (let $ignore
        (collapse
          (match $src (: $x $a)
            (add-atom $dst (: $x $a))))
        ()))

(= (quiet-fs-add-to-ss $rb $ss)
   (let $ignore
        (collapse (fs-add-to-ss $rb $ss))
        ()))

!(bind! &rb (new-space))
!(add-atom &rb (: ax-mp (-> (→ $𝜑 $𝜓) $𝜑 $𝜓)))

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))
!(add-atom &ab (: ax-2 (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))))

!(bind! &ss (new-space))
!(quiet-copy-space &ab &ss)
!(quiet-fs-add-to-ss &rb &ss)
!(quiet-fs-add-to-ss &rb &ss)
!(assertEqualToResult (count-atoms &ss) (72))
