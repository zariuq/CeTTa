; Nil Geisweiller's propositional calculus forward chainer — depth 4
; WARNING: ~11.7M pairs at step 4. May take minutes.

;; Depth parameter
(= (depth) 4)

;; Nat
(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

;; Forward step: binary application of a rule to two premises
(= (fs2 $rb (: $x1 $a1) (: $x2 $a2))
   (match $rb (: $f (-> $a1 $a2 $b)) (: ($f $x1 $x2) $b)))

;; Forward step over all premises in the solution set
(= (fs $rb $ss)
   (fs2 $rb
        (match $ss (: $x1 $a1) (: $x1 $a1))
        (match $ss (: $x2 $a2) (: $x2 $a2))))

;; Forward step + add results back to solution space
(= (fs-add-to-ss $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: $x1 $a1)
       (match $ss0 (: $x2 $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (add-atom $ss (: ($f $x1 $x2) $b)))))))

;; Rule base: modus ponens
!(bind! &rb (new-space))
!(add-atom &rb (: ax-mp (-> (→ $p $q) $p $q)))

;; Axiom base: propositional calculus
!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $p (→ $q $p))))
!(add-atom &ab (: ax-2 (→ (→ $p (→ $q $r)) (→ (→ $p $q) (→ $p $r)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $p) (¬ $q)) (→ $q $p))))

;; Solution set
!(bind! &ss (new-space))

;; Step 0: seed with axioms
!(match &ab (: $x $a) (add-atom &ss (: $x $a)))

;; Step 1
!(if (<= 1 (depth)) (fs-add-to-ss &rb &ss) (empty))

;; Step 2
!(if (<= 2 (depth)) (fs-add-to-ss &rb &ss) (empty))

;; Step 3
!(if (<= 3 (depth)) (fs-add-to-ss &rb &ss) (empty))

;; Step 4
!(if (<= 4 (depth)) (fs-add-to-ss &rb &ss) (empty))

;; Show count using CeTTa's count-atoms extension
;; (not get-atoms — too large to materialize)
!(count-atoms &ss)
