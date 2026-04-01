; Variant cache mode should flow through pragma!-recorded session options
; and preserve recursive backchaining semantics on the current
; HE-compatible lane.

!(pragma! search-table-mode variant)

(Evaluation (philosopher Plato))
(Evaluation (likes-to-wrestle Plato))

(Implication
   (And (Evaluation (philosopher $x))
        (Evaluation (likes-to-wrestle $x)))
   (Evaluation (human $x)))

(Implication
   (Evaluation (human $x))
   (Evaluation (mortal $x)))

(= (deduce (Evaluation ($P $x)))
   (match &self (Evaluation ($P $x)) T))

(= (deduce (Evaluation ($P $x)))
   (match &self
     (Implication $a (Evaluation ($P $x)))
     (deduce $a)))

(= (deduce (And $a $b))
   (And (deduce $a) (deduce $b)))

(= (And T T) T)
(= (ift T $then) $then)

!(assertEqual
  (deduce (Evaluation (mortal Plato)))
  T)

!(assertEqual
  (ift (deduce (Evaluation (mortal $x))) $x)
  Plato)
