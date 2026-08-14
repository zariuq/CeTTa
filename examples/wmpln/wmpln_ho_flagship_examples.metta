!(import! &self ../../lib/lib_wmpln)

; Flagship executable shadows for the HO-WM-PLN theorem interfaces.
;
; These examples mirror the Lean results around:
; - width zero iff a classical-Henkin theory decides the represented formula,
; - predicate implication / inheritance transitivity,
; - saturated predicate similarity as exact-threshold analogy transfer.
;
; The expected values are hand-derived.  This file is not a proof oracle; Lean
; is the proof authority, and this is the runnable PLN-facing story.

(= (Interval $lower $upper) (interval $lower $upper))
(= (interval-lower (interval $lower $upper)) $lower)
(= (interval-upper (interval $lower $upper)) $upper)
(= (interval-width (interval $lower $upper)) (- $upper $lower))

; Decidedness witness:
; A proved formula has endpoint interval [1,1]. An open formula over two
; completions has [0,1].  Width is therefore deductive indeterminacy.
(= (theory-interval DECIDES-MOTHER-PARENT) (Interval 1.0 1.0))
(= (theory-interval OPEN-PARENT-EMPLOYED) (Interval 0.0 1.0))

!(assertEqual (interval-width (theory-interval DECIDES-MOTHER-PARENT)) 0.0)
!(assertEqual (interval-width (theory-interval OPEN-PARENT-EMPLOYED)) 1.0)
!(assertEqual
  (== (interval-width (theory-interval OPEN-PARENT-EMPLOYED)) 0.0)
  False)

; Predicate-level HO inheritance chain:
; mother -> parent and parent -> caregiver entail mother -> caregiver.
(= (ho-inherits MOTHER PARENT) True)
(= (ho-inherits PARENT CAREGIVER) True)
(= (ho-inherits MOTHER CAREGIVER) True)
(= (ho-inherits CAREGIVER MOTHER) False)

(= (ho-inheritance-chain $a $b $c)
   (and (ho-inherits $a $b) (ho-inherits $b $c)))

!(assertEqual (ho-inheritance-chain MOTHER PARENT CAREGIVER) True)
!(assertEqual (ho-inherits MOTHER CAREGIVER) True)
!(assertEqual (ho-inheritance-chain CAREGIVER MOTHER PARENT) False)

; Exact-threshold analogy transfer:
; Similarity at threshold 1 is mutual predicate implication.  If two
; predicates are saturated-similar and the source has a saturated inheritance,
; the target inherits too.  Missing similarity blocks the transfer.
(= (ho-similar PREDATOR CARNIVORE) True)
(= (ho-similar CARNIVORE PREDATOR) True)
(= (ho-similar BIRD AIRPLANE) False)
(= (ho-similar AIRPLANE BIRD) False)

(= (ho-inherits PREDATOR HUNTS) True)
(= (ho-inherits CARNIVORE HUNTS) True)
(= (ho-inherits AIRPLANE FLIES) True)
(= (ho-inherits BIRD FLIES) False)
(= (ho-inherits PREDATOR CARNIVORE) True)
(= (ho-inherits CARNIVORE PREDATOR) True)

(= (analogy-transfer $source $target $property)
   (and (ho-similar $source $target) (ho-inherits $source $property)))

!(assertEqual (analogy-transfer PREDATOR CARNIVORE HUNTS) True)
!(assertEqual (ho-inherits CARNIVORE HUNTS) True)
!(assertEqual (analogy-transfer AIRPLANE BIRD FLIES) False)
!(assertEqual
  (== (analogy-transfer AIRPLANE BIRD FLIES) (ho-inherits BIRD FLIES))
  True)

; Joint-cut analogy transfer:
; The Lean joint-cut theorems package both saturated premises into one
; conjunctive definable cut before applying the existing analogy theorem.
(= (joint-source-analogy-cut $source $target $property)
   (and (ho-similar $source $target) (ho-inherits $source $property)))
(= (rule-implication-cut $premise $conclusion)
   (if $premise $conclusion True))
(= (source-analogy-rule-cut $source $target $property)
   (rule-implication-cut
     (joint-source-analogy-cut $source $target $property)
     (ho-inherits $target $property)))

!(assertEqual (joint-source-analogy-cut PREDATOR CARNIVORE HUNTS) True)
!(assertEqual (ho-inherits CARNIVORE HUNTS) True)
!(assertEqual (joint-source-analogy-cut AIRPLANE BIRD FLIES) False)
!(assertEqual (source-analogy-rule-cut PREDATOR CARNIVORE HUNTS) True)
!(assertEqual (source-analogy-rule-cut AIRPLANE BIRD FLIES) True)

; A synthetic counterexample check for the implication gate itself: if the joint
; saturated premises were present but the conclusion failed, the rule cut would
; reject.  The Lean rule-cut theorem says this counterexample cannot occur for
; the certified exact-threshold analogy theorem.
(= (ho-similar DOG HOUND) True)
(= (ho-inherits DOG BARKS) True)
(= (ho-inherits HOUND BARKS) False)
!(assertEqual (joint-source-analogy-cut DOG HOUND BARKS) True)
!(assertEqual (source-analogy-rule-cut DOG HOUND BARKS) False)

; Similarity / inheritance conversion:
; Saturated similarity is mutual predicate implication, so exact-threshold
; similarity converts to both directed inheritance cuts.  Conversely, the joint
; directed inheritance cut converts back to similarity.
(= (similarity-to-forward-inheritance-rule $a $b)
   (rule-implication-cut (ho-similar $a $b) (ho-inherits $a $b)))
(= (similarity-to-reverse-inheritance-rule $a $b)
   (rule-implication-cut (ho-similar $a $b) (ho-inherits $b $a)))
(= (mutual-inheritance-joint $a $b)
   (and (ho-inherits $a $b) (ho-inherits $b $a)))
(= (mutual-inheritance-to-similarity-rule $a $b)
   (rule-implication-cut (mutual-inheritance-joint $a $b) (ho-similar $a $b)))

!(assertEqual (similarity-to-forward-inheritance-rule PREDATOR CARNIVORE) True)
!(assertEqual (similarity-to-reverse-inheritance-rule PREDATOR CARNIVORE) True)
!(assertEqual (mutual-inheritance-to-similarity-rule PREDATOR CARNIVORE) True)
!(assertEqual (similarity-to-forward-inheritance-rule AIRPLANE BIRD) True)

; Synthetic guardrails: these violate the theorem hypotheses semantically and
; therefore make the rule implication itself reject.
(= (ho-similar SWAN GOOSE) True)
(= (ho-inherits SWAN GOOSE) False)
!(assertEqual (similarity-to-forward-inheritance-rule SWAN GOOSE) False)

(= (ho-inherits OTTER MUSTELID) True)
(= (ho-inherits MUSTELID OTTER) True)
(= (ho-similar OTTER MUSTELID) False)
!(assertEqual (mutual-inheritance-to-similarity-rule OTTER MUSTELID) False)

(= (ho-inherits WOLF PREDATOR) True)
(= (ho-inherits WOLF CARNIVORE) True)
(= (ho-inherits AIRPLANE BIRD) False)
(= (ho-similar BIRD CARNIVORE) False)

(= (joint-target-analogy-cut $source $oldTarget $newTarget)
   (and (ho-inherits $source $oldTarget) (ho-similar $oldTarget $newTarget)))
(= (target-analogy-rule-cut $source $oldTarget $newTarget)
   (rule-implication-cut
     (joint-target-analogy-cut $source $oldTarget $newTarget)
     (ho-inherits $source $newTarget)))

!(assertEqual (joint-target-analogy-cut WOLF PREDATOR CARNIVORE) True)
!(assertEqual (ho-inherits WOLF CARNIVORE) True)
!(assertEqual (joint-target-analogy-cut AIRPLANE BIRD CARNIVORE) False)
!(assertEqual (target-analogy-rule-cut WOLF PREDATOR CARNIVORE) True)
!(assertEqual (target-analogy-rule-cut AIRPLANE BIRD CARNIVORE) True)

; Synthetic target-side counterexample guardrail.
(= (ho-inherits CAT FELINE) True)
(= (ho-similar FELINE MAMMALISH) True)
(= (ho-inherits CAT MAMMALISH) False)
!(assertEqual (joint-target-analogy-cut CAT FELINE MAMMALISH) True)
!(assertEqual (target-analogy-rule-cut CAT FELINE MAMMALISH) False)
