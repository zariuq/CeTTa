;; Rows 05/06/07 — Nil Geisweiller: deep propositional-calculus theorems.
;; Source: repos/ngeiswei-chaining-run/experimental/backward-via-forward/bfc-xp.metta
;;   (obc = size-optimized backward chainer; obfc = optimized backward-via-forward).
;; Both are self-contained (axioms ax-1/ax-2/ax-3 + mp matched directly in the
;; equations, no KB space needed). Łukasiewicz axioms; naive bc would not
;; terminate (mp spawns fresh metavars), hence the size/forward bounding.
;; Theorems: jarr (d13), pm2.27 (d13), imim1 (d15). Runs on CeTTa (--lang he) and PeTTa.

;; ---- Size-optimized backward chainer (Number-indexed) ----
(= (obc-gtz $s (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))
   (MkSized 1 (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑)))))
(= (obc-gtz $s (: ax-2 (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))
   (MkSized 1 (: ax-2 (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))))))
(= (obc-gtz $s (: ax-3 (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))))
   (MkSized 1 (: ax-3 (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑)))))
(= (obc-gtz $s (: (mp $f $x) $b))
   (if (< 2 $s)
       (let* (((MkSized $fs (: $f (→ $a $b))) (obc (- $s 2) (: $f (→ $a $b))))
              ((MkSized $xs (: $x $a)) (obc (- (- $s 1) $fs) (: $x $a))))
         (MkSized (+ (+ $fs $xs) 1) (: (mp $f $x) $b)))
       (empty)))
(= (obc $s (: $x $a))
   (if (< 0 $s)
       (obc-gtz $s (: $x $a))
       (empty)))

;; ---- Optimized backward chainer via forward chaining ----
(= (ofc $depth $hypcnt $tgt (: $x $a))
   (if (< 0 $hypcnt)
       (if (and (< 0 $depth) (<= $hypcnt $depth))
           (ofc-rec (- $depth 1) (- $hypcnt 1) $tgt (: $x $a))
           (empty))
       (let (: $x $a) $tgt (: $x $a))))
(= (ofc-rec $depth $hypcnt $tgt (: $f (-> (→ $𝜑 (→ $𝜓 $𝜑)) $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax-1) $b)))
(= (ofc-rec $depth $hypcnt $tgt
                (: $f (-> (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))) $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax-2) $b)))
(= (ofc-rec $depth $hypcnt $tgt
                (: $f (-> (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑)) $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax-3) $b)))
(= (ofc-rec $depth $hypcnt $tgt (: $f (-> $b $c)))
   (ofc $depth (+ $hypcnt 2) $tgt (: (mpⁱ $f) (-> (→ $a $b) (-> $a $c)))))
(= (obfc $depth (: $x $a))
   (ofc $depth 1 (: $x $a) (: id (-> $a $a))))

;; ---- Deep theorem queries (obc lane) ----
!(obc 13 (: $x (→ (→ (→ 𝜑 𝜓) 𝜒) (→ 𝜓 𝜒))))
!(obc 13 (: $x (→ 𝜑 (→ (→ 𝜑 𝜓) 𝜓))))
!(obc 15 (: $x (→ (→ 𝜑 𝜓) (→ (→ 𝜓 𝜒) (→ 𝜑 𝜒)))))
