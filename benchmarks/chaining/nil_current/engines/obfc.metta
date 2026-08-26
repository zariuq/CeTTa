;; Generated file; do not edit.
;; Nil source revision: 2151416f4d44b3b2dbbcd57a0dd7ffd5a75c992c
;; Nil source path: experimental/backward-via-forward/obfc-xp.metta
;; Source blob SHA-256: eddac8b756f12757881cf25fd75932e016d1012bb918c58ecce17fc47a46b764
;; Structural slice: before: Reduction + section: Optimized Forward Chainer..Main

;; Backward via forward chaining.  It works by forwadly generating
;; hypothetical truths (and their associated proof abstractions)
;; instead of absolute truths (and their associated proofs), then,
;; still going forward, by applying these hypothetical truths to the
;; axioms to reach absolute truths.  The idea being that only
;; hypothetical truth proving given theorems are produced and nothing
;; else.
;;
;; Hypothetical truths are produced using dedicated combinators.
;;
;; By combining the bluebird and blackbird
;; [combinators](https://hackage.haskell.org/package/data-aviary-0.4.0/docs/src/Data-Aviary-Birds.html)
;; one can apply modus ponens backward while doing forward chaining.
;;
;; The blackbird operator, B₁, can be used to create a specialized
;; rule to expand modus ponens backward on the first argument of any
;; abstract proof:
;;
;; mpⁱ = (λ z ((B₁ z) mp))
;;
;; with type signature
;;
;; (-> (-> $b $c) (-> (→ $a $b) (-> $a $c)))
;;
;; or in Haskell
;;
;; (b -> c) -> ((→ a b) -> a -> c)
;;
;; meaning this rule can turn any abstract proof of type (b -> c) into
;; am abstract proof of type (→ a b) -> a -> c.  In other words b is
;; replaced by (→ a b) -> a, i.e. a monus ponens expansion on the
;; first argument.
;;
;; Then the bluebird can be used to move this specialized rule to
;; expand modus ponens backward to any argument of any abstract proof:
;;
;; (. mpⁱ) to expand the second argument, (. (. mpⁱ)) to expand the
;; third argument, etc.
;;
;; For more naming conventions of combinators, see
;; https://wiki.xxiivv.com/site/logic.html

;; Disable co-unification
!(translatePredicate (set_prolog_flag occurs_check True))

;;;;;;;;;;;;;;;;;;;;;
;; ;;;;;;;;;;;;;;; ;;
;; ;; Functions ;; ;;
;; ;;;;;;;;;;;;;;; ;;
;;;;;;;;;;;;;;;;;;;;;

;;;;;;;;;;;;;;;

;; Optimized Forward Chainer ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

!(bind! &ofc-sol (new-space))

;; Optimized forward chainer meant to emulate backward chaining.  It
;; is optimized in the senses that
;;
;; 1. It uses proof size instead of depth (although in this proof
;; representation they are one and the same).
;;
;; 2. It prunes the search by ignoring exploring branches that may not
;; lead to non hypotheses in the final result.
;;
;; 3. It uses Number instead of Nat.
;;
;; 4. It is specialized for propositional calculus.
;; (: ofc (-> Number                    ; Maximum depth
;;            Number                    ; Hypotheses count
;;            $b                        ; Target query
;;            $b                        ; Source query
;;            $b))
(= (ofc $depth $hypcnt $tgt (: $x $a))
   (if (< 0 $hypcnt)
       ;; Recursive steps
       (if (and (< 0 $depth) (<= $hypcnt $depth))
           (progn
            ;; (add-atom &ofc-sol (sol $depth $hypcnt (: $x $a)))
            (ofc-rec (- $depth 1) (- $hypcnt 1) $tgt (: $x $a)))
           (empty))
       ;; Base case
        (let (: $x $a) $tgt
             (progn
              ;; (add-atom &ofc-sol (sol $depth $hypcnt (: $x $a)))
              (: $x $a)))))

;; Recursive steps (note that depth and hypcnt have already been
;; decremented) Specialized application given abstraction
(= (ofc-rec $depth_dec $hypcnt_dec $tgt
            (: $f (-> (→ $𝜑 (→ $𝜓 $𝜑)) $b)))
   (ofc $depth_dec $hypcnt_dec $tgt (: ($f ax₁) $b)))
(= (ofc-rec $depth_dec $hypcnt_dec $tgt
            (: $f (-> (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))) $b)))
   (ofc $depth_dec $hypcnt_dec $tgt (: ($f ax₂) $b)))
(= (ofc-rec $depth_dec $hypcnt_dec $tgt
            (: $f (-> (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑)) $b)))
   (ofc $depth_dec $hypcnt_dec $tgt (: ($f ax₃) $b)))
;; Specialized application given argument of mpⁱ
(= (ofc-rec $depth_dec $hypcnt_dec $tgt (: $f (-> $b $c)))
   (ofc $depth_dec (+ $hypcnt_dec 2) $tgt (: (mpⁱ $f) (-> (→ $a $b) (-> $a $c)))))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Optimized backward chainer via forward chaining ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Backward via forward chaining
(: (obfc (-> Number                     ; Maximum depth
             $b                         ; Query
             $b)))
(= (obfc $depth (: $x $a))
   (ofc $depth 1 (: $x $a) (: I (-> $a $a))))

;;;;;;;;;;;;;;;;
;; ;;;;;;;;;; ;;
