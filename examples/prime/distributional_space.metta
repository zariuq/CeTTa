;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; A finite distributional-space experiment                                 ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;
;;   ./cetta --lang prime examples/prime/distributional_space.metta
;;
;; A space can interpret atoms against a JOINT model.  Storing one probability
;; per atom loses correlations.  This executable sketch uses immutable tables;
;; it does not install a new C Space backend or certify the runtime in Lean.
;;
;; World(rain, wet, weight) describes a possible world.  Weights are nonnegative
;; integers and need not sum to one.  Query probabilities are exact fractions.
;; A richer backend could represent the same model by factors or a Bayesian
;; network without enumerating all worlds.

(= (ds:joint)
   ((World True True 18) (World True False 2)
    (World False True 8) (World False False 72)))

(= (ds:rain) (lam r (lam w r)))
(= (ds:wet) (lam r (lam w w)))
(= (ds:certain) (lam r (lam w True)))
(= (ds:both) (lam r (lam w (and r w))))

(= (ds:mass $worlds $event)
   (if (== $worlds ())
       0
       (let ((World $r $w $n) $rest) (decons-atom $worlds)
         (let $more (ds:mass $rest $event)
           (if ($event $r $w) (+ $n $more) $more)))))

(= (ds:gcd $a $b)
   (if (== $b 0) $a (ds:gcd $b (% $a $b))))
(= (ds:ratio $n $d)
   (if (== $d 0)
       (Undefined zero-mass)
       (let $g (ds:gcd $n $d) (ratio (// $n $g) (// $d $g)))))
(= (ds:prob $worlds $event)
   (ds:ratio (ds:mass $worlds $event) (ds:mass $worlds (ds:certain))))

;; Reweighting is Bayesian observation by a likelihood, not storage insertion.
;; Hard observation is the special case whose likelihood is 0 or 1.
(= (ds:reweight $worlds $likelihood)
   (if (== $worlds ())
       ()
       (let ((World $r $w $n) $rest) (decons-atom $worlds)
         (let $scaled (* $n ($likelihood $r $w))
           (cons-atom (World $r $w $scaled) (ds:reweight $rest $likelihood))))))
(= (ds:wet-likelihood) (lam r (lam w (if w 1 0))))

!(Prior-rain (ds:prob (ds:joint) (ds:rain)))
!(Prior-wet (ds:prob (ds:joint) (ds:wet)))
!(Joint-rain-and-wet (ds:prob (ds:joint) (ds:both)))
!(Rain-given-wet
   (ds:prob (ds:reweight (ds:joint) (ds:wet-likelihood)) (ds:rain)))

;; Same marginal, different future answer.  Each model puts half its mass on
;; rain, but observing wet gives P(rain | wet) = 1 in one and 0 in the other.
;; The Lean companion proves that NO function of the rain marginal alone can
;; implement this update for every joint model.
(= (ds:correlated)
   ((World True True 1) (World False False 1)))
(= (ds:anticorrelated)
   ((World True False 1) (World False True 1)))

!(Same-marginals
   (ds:prob (ds:correlated) (ds:rain))
   (ds:prob (ds:anticorrelated) (ds:rain)))
!(Different-after-wet
   (ds:prob (ds:reweight (ds:correlated) (ds:wet-likelihood)) (ds:rain))
   (ds:prob (ds:reweight (ds:anticorrelated) (ds:wet-likelihood)) (ds:rain)))

;; A positive law complements the counterexample: compression and reweighting
;; commute if the likelihood depends only on the retained observation.  The
;; Lean theorem reweight_commutes_iff also proves necessity when the law must
;; hold for every input weight.  A richer retained state could store the
;; conditional information needed to support additional likelihoods.

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Evidence identity and revision ;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Replaying the SAME evidence must not count it twice.  Here identifiers are
;; immutable and uniquely identify a likelihood.  An update with a new payload
;; under an existing identifier would need explicit replacement, not replay.
;; A production ledger should retain the likelihoods, dependencies and model
;; revision too; this short example retains identifiers and the resulting mass.
(= (ds:seen $id $ids)
   (if (== $ids ())
       False
       (let ($first $rest) (decons-atom $ids)
         (if (== $first $id) True (ds:seen $id $rest)))))
(= (ds:observe (Ledger $worlds $ids) $id $likelihood)
   (if (ds:seen $id $ids)
       (Ledger $worlds $ids)
       (Ledger (ds:reweight $worlds $likelihood) (cons-atom $id $ids))))
(= (ds:answer (Ledger $worlds $ids)) (Answer (ds:prob $worlds (ds:rain)) $ids))

;; This sensor outcome has likelihood ratio 9:1 for rain.  Multiplying all
;; likelihoods by a common positive constant does not change its posterior.
(= (ds:sensor) (lam r (lam w (if r 9 1))))
(= (ds:initial) (Ledger (ds:joint) ()))
(= (ds:first) (ds:observe (ds:initial) sensor-1 (ds:sensor)))

!(First-evidence (ds:answer (ds:first)))
!(Same-evidence-replayed
   (ds:answer (ds:observe (ds:first) sensor-1 (ds:sensor))))

;; Two distinct identifiers DO NOT establish independence.  This calculation
;; assumes the two sensor outcomes are conditionally independent given rain.
;; Correlated sensors need a joint likelihood instead.
!(Second-independent-evidence
   (ds:answer (ds:observe (ds:first) sensor-2 (ds:sensor))))

;; Conditioning on an impossible event is undefined, not probability zero and
;; not logical refutation.  The finite prototype makes that boundary visible.
!(Impossible-observation
   (ds:prob
     (ds:reweight (ds:joint) (lam r (lam w 0)))
     (ds:rain)))
