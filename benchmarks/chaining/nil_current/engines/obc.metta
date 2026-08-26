;; Generated file; do not edit.
;; Nil source revision: 2151416f4d44b3b2dbbcd57a0dd7ffd5a75c992c
;; Nil source path: experimental/backward-via-forward/obc-xp.metta
;; Source blob SHA-256: f502c32358a54b96eafa6bf0515aa51277b18e88b216f30e42e7b827f3a5fae7
;; Structural slice: before: Test proof-sized bound optimized backward chainer

;; Optimized Backward Chainer with Maximum Proof Size.
;;
;; Extracted from bfc-xp.metta so that this implementation can be
;; loaded and benchmarked independently from the rest of the
;; backward-via-forward experiments.  Specialized for propositional
;; calculus (axioms ax₁, ax₂, ax₃ and inference rule mp only).
;;
;; Self-contained: the `Sized`/`MkSized` types that `obc` depends on
;; are defined locally (originally borrowed from
;; experimental/metamath/pc-xp.metta).

;; Disable co-unification
!(translatePredicate (set_prolog_flag occurs_check True))

;;;;;;;;;;;;;;;
;; Sized ;;
;;;;;;;;;;;;;;;

;; Associate a size to a term
(: Sized (-> $a Type))
(: MkSized (-> $n                       ; Size
               $a                       ; Object
               (Sized $a)))             ; Sized object

;; Access functions
(= (Sized.size (MkSized $s $x)) $s)
(= (Sized.object (MkSized $s $x)) $x)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Optimized backward chainer with maximum proof size ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Optimized backward chainer, in the sense that it uses maximum proof
;; size instead of depth, is specialized for propositional calculus
;; and uses Number instead of Nat.
(= (obc-gtz $s (: ax₁ (→ $𝜑 (→ $𝜓 $𝜑))))
   (MkSized 1 (: ax₁ (→ $𝜑 (→ $𝜓 $𝜑)))))
(= (obc-gtz $s (: ax₂ (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))
   (MkSized 1 (: ax₂ (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))))))
(= (obc-gtz $s (: ax₃ (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))))
   (MkSized 1 (: ax₃ (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑)))))
(= (obc-gtz $s (: (mp $f $x) $b))
   (if (< 2 $s) ; The budget must be at least 3
       (let* (((MkSized $fs (: $f (→ $a $b))) (obc (- $s 2) (: $f (→ $a $b))))
              ((MkSized $xs (: $x $a)) (obc (- (- $s 1) $fs) (: $x $a))))
         (MkSized (+ (+ $fs $xs) 1) (: (mp $f $x) $b)))
       (empty)))
(= (obc $s (: $x $a))
   (if (< 0 $s) ; The budget must be at least 1
       (obc-gtz $s (: $x $a))
       (empty)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
