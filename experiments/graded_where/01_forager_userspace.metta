; ============================================================================
; Graded where, user-space edition (runs today): the mortal forager
; ============================================================================
;
; L.G. Meredith's running example (MeTTaIL for the Working Software
; Developer, §5.6), assembled the only way current MeTTa can assemble it:
; every graded mechanic is hand-built in ordinary code.  The graded
; where-clause machinery this file stands in for:
;
;   (= (open $spec)   (body)   (grade (power belief)))   ; candidate rule A
;   (= (graze $spec)  (body)   (grade (graze-value)))    ; candidate rule B
;   (discipline argmax)                                   ; race resolution
;
; Places where an engine value-slot would absorb hand-written plumbing are
; marked [WISHLIST].  The theorem base for what is and is not sugar is
; Mettapedia/lean/mettapedia/Mettapedia/GSLT/Core/GradedSelectionIrreducibility.lean
; (crisp where ≡ if-in-transform: proved; normalized shares need the race
; total: proved; value/cost two axes: proved).
;
; World (deterministic, HOSTILE): specimens are rich 5% of the time on a
; fixed period hidden from the forager; every specimen passes the cheap
; structural test chi (the trap — chi is evidence, not proof).  With
; payoff +2 / -1 and wall +0.2, grazing is the right policy here — but the
; forager does not know that: it must EARN the knowledge, and pay for it.
; The belief is a PLN simple truth value (strength s, evidence count n) held
; as ordinary data; the graded weight s·c with c = n/(n+k) is COMPUTED per
; step from the belief atoms, never stored ("plasticity is free": edit the
; belief atoms and every clause that reads them re-values with no update
; machinery).

; ---------------------------------------------------------------------------
; 1. The graded vocabulary — this is what arrives weighted at the race
; ---------------------------------------------------------------------------

; scepticism k: how much evidence moving the strength takes
(= (k-scepticism) 4.0)

(= (confidence $n) (/ $n (+ $n (k-scepticism))))

; The PLN power s·c: "how much this candidate happens".
; This value lives in the weight semiring (R≥0, × for conjunction,
; + for disjunction).  Bool is the crisp corner: (w ≠ 0) = admitted.
(= (power $n $s) (* $s (confidence $n)))

; ---------------------------------------------------------------------------
; 2. The world (hidden from the forager)
; ---------------------------------------------------------------------------

(= (specimen $i) (if (== (% $i 20) 19) rich decoy))   ; 5% rich, period 20
(= (chi $spec) True)                                   ; chi always "maybe rich"

(= (outcome rich) 1)
(= (outcome decoy) 0)

(= (payoff rich) 2)
(= (payoff decoy) -1)

(= (graze-value) 0.045)                                ; the wall's standing weight
(= (graze-payoff) 0.2)

; PLN revision by one Bernoulli observation x ∈ {0,1}:
; (n, s) ↦ (n+1, (n·s + x)/(n+1)).  (The kernel-checked reading
; of this step: an implicit natural-gradient step on the log-likelihood,
; Mettapedia/PLN/Bridges/GSLT/RevisionNaturalGradient.lean.)
(= (revise $n $s $x) ((+ $n 1) (/ (+ (* $n $s) $x) (+ $n 1))))

; ---------------------------------------------------------------------------
; 3. One step of life — three policies over the same world
; ---------------------------------------------------------------------------
;
; [WISHLIST] Candidates open/graze compete for the specimen.  The graded
; policy writes this competition as an if-then-else BY HAND: the race set
; (and hence any normalizer a `sample` discipline would need — the race
; total, cf. raceShare_eq_of_total_eq) never exists as a value.
;
; [WISHLIST] With graded where + a per-space discipline this step is zero
; rules of plumbing: engine enumerates candidates, reads where-values,
; resolves by the declared discipline, commits the winner.

; GRADED policy: the race is decided by weights (argmax by hand).
(= (step-graded $n $s $holdings $i)
   (let $spec (specimen $i)
     (let $w (power $n $s)
       (if (and (chi $spec) (> $w (graze-value)))
           (let ($n2 $s2) (revise $n $s (outcome $spec))
             (($n2 $s2 (+ $holdings (payoff $spec)))
              (opened $spec for-weight $w)))
           (($n $s (+ $holdings (graze-payoff)))
            (grazed-over $spec at-weight $w))))))

; CRISP policy: the classic where-clause — open exactly when chi passes.
; This is the (proved) sugar direction: (= open $spec body (chi $spec)) is
; exactly (= open $spec (if (chi $spec) body (empty))).
(= (step-crisp $n $s $holdings $i)
   (let $spec (specimen $i)
     (if (chi $spec)
         (let ($n2 $s2) (revise $n $s (outcome $spec))
           (($n2 $s2 (+ $holdings (payoff $spec)))
            (opened $spec crisp)))
         (($n $s (+ $holdings (graze-payoff)))
          (grazed-over $spec crisp)))))

; ---------------------------------------------------------------------------
; 4. The loop of life (state threaded by recursion — itself [WISHLIST]: the
;    forager's loop lives in user code because the ambient engine offers no
;    committing agent step with a declared discipline)
; ---------------------------------------------------------------------------

(= (run $step $n $s $holdings $i $stop $trace)
   (if (>= $i $stop)
       (done $holdings (rev-list $trace))
       (let ($state2 $action) ($step $n $s $holdings $i)
         (let ($n2 $s2 $h2) $state2
           (let $i2 (+ $i 1)
             (run $step $n2 $s2 $h2 $i2 $stop
                  (cons-atom ($i belief ($n2 $s2) $action) $trace)))))))

(= (rev-list $xs) (rev-onto $xs ()))
(= (rev-onto $xs $acc)
   (if (== $xs ()) $acc
       (rev-onto (cdr-atom $xs) (cons-atom (car-atom $xs) $acc))))

; ---------------------------------------------------------------------------
; 5. The three phenomena, measured
; ---------------------------------------------------------------------------

; The bootstrap trap, as a weight — not a verdict:
!(power 0 0.5)     ; = 0.0 — no evidence ⇒ confidence 0 ⇒ the open candidate
                   ; grades 0 and never wins (it is not REJECTED; zero is
                   ; abstention, Greg's "no transition")
!(power 1 0.5)     ; = 0.1 — one unit of prior evidence lifts the weight
                   ; above the wall, and opening begins

; Forager BLIND (graded policy, prior evidence n = 0):
; never opens; terminal holdings = 30 · 0.2 = 6.0 — the grazing baseline.
; Safe, and learns nothing.
!(run step-graded 0 0.5 0 0 30 ())

; Forager LEARNER (graded policy, prior n = 1, s = 0.5):
; opens; seven straight decoys drag s·c through the wall; then it STOPS,
; belief frozen, grazing safely.  Expect 7 opens then 23 grazes,
; terminal holdings = -7 + 23·0.2 = -2.4.
!(run step-graded 1 0.5 0 0 30 ())

; Forager CRISP (the classic where-clause):
; opens whenever chi passes — always.  29 decoys and 1 rich specimen later:
; terminal holdings = -29 + 2 = -27.  It could not do otherwise: its policy
; is the formula, and the formula does not change.  A Boolean guard has no
; coordinate in which to record disappointment.
!(run step-crisp 1 0.5 0 0 30 ())
