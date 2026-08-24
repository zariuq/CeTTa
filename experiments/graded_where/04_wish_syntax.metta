; ============================================================================
; WISH SYNTAX — specification artifact (NOT runnable today)
; ============================================================================
;
; The proposed graded where-clause surface for a native --lang prime engine,
; against which the in-language engine of 03_graded_engine_in_language.metta
; is the semantics definition and files 01–02 are the manual approximations.
; Design tenets already ratified/proved:
;
;   - crisp where ≡ if-in-transform            (Lean: whereDeliver_eq_ifDeliver)
;   - Bool corner lives in the DISCIPLINE      (this file, run C vs run B in 03)
;   - normalized disciplines need the race     (Lean: normalizedShare_not_
;     total; streams need windows/decay         candidatewise, raceShare_eq_
;                                                of_total_eq)
;   - value and cost are two axes              (Lean: grade_annihilation_
;                                                cannot_gate)
;   - reduction/data discipline uniform        (semantics_findings note, CP2)
;
; ---------------------------------------------------------------------------
; 1. Authored graded rules: two spellings, one meaning
; ---------------------------------------------------------------------------

; Trailing grade annotation (preferred: arity-safe, reads naturally):
(= (decide) (open-outcome)  (grade (fetch-power)))
(= (decide) (graze-outcome) (grade (graze-value)))

; Arity-3 shorthand (same judgment; arity disambiguates from 2-ary '='):
(= (decide) (open-outcome)  (fetch-power))
(= (decide) (graze-outcome) (graze-value))

; Lint (from the ratified design): an engine whose profile lacks conditions
; loads an arity-3/annotated rule as inert data — fail CLOSED, and warn:
; "graded rule in a space without a declared discipline".

; The grade expression: any ordinary expression over the pattern bindings and
; ambient (read-only, PURE) space queries.  Its type is the space's declared
; weight semiring V; Bool is the degenerate corner.  Conjunction of
; conditions multiplies (⊗); alternatives add (⊕); a derivation's path
; weight is the ⊗-product along it; the bag accumulates ⊕.
; Weights are RAW — not normalized: "sums to 1" is a property of the
; 'sample' family only, and only at the sampling moment (the race total
; appears in the denominator there and nowhere else).

; ---------------------------------------------------------------------------
; 2. Disciplines: named, per space, pluggable
; ---------------------------------------------------------------------------

(discipline &forager (argmax-by grade))         ; the forager's race
(discipline &forager (first-nonzero))           ; the classic crisp where
;                                               ;  (bool corner, zero = abstain)
(discipline &kb      (threshold 0.3))           ; keep-all above a bound
(discipline &frontier (best-k 5 (by heuristic))) ; select-best-k with a
;                                               ;  PLUGGABLE ORDER/EVALUATOR:
;                                               ;  'heuristic' is just what a
;                                               ;  where-clause already is
(discipline &lab     (sample))                  ; Gillespie family: needs
;                                               ;  the race total => finite
;                                               ;  bags only; on a stream
;                                               ;  space the engine must
;                                               ;  REJECT this (typing!)
(discipline &stream  (beam 2))                  ; bounded window: stream-native
(discipline &stream  (decayed 0.95 (argmax-by grade)))
;                                               ; forgetting as a discipline:
;                                               ; grades discounted per step,
;                                               ; the anti-rigidity cure

; Semantics anchor: each is a line in section 1 of file 03 (in-language),
; or a line of section 3 of file 02 (reified).  Lanes: the space-level
; declared discipline for ambient execution; reified-frontier programming
; (strategies as data) for provers and search control.  Both, not either.

; ---------------------------------------------------------------------------
; 3. The loop of life, ambient form
; ---------------------------------------------------------------------------

; Today's file 03 already shows the agent loop running through the engine.
; The native ambition: the ambient evaluator's own commit step is the
; loop — rules fire into a space, beliefs update, grades revalue
; ("plasticity is free": the weight is computed, never cached) — and the
; runner of section 04 of file 03 disappears for the standard case.
; Epoch-tagged belief rows (append-only) remain the provenance pattern.
(= (life &forager)
   (forever (fire &forager)))                   ; sketch only

; ---------------------------------------------------------------------------
; 4. Streams: where the design must stay honest
; ---------------------------------------------------------------------------

; A stream space never materializes the whole race set, so normalized
; disciplines are ill-posed there (no total).  What survives:
;   - first-nonzero, threshold, argmax, best-k, beam (windows)
;   - decayed γ (the decay IS a stream total: a convergent geometric trace)
; What must not silently pretend:
;   - (sample) over a stream — the engine rejects, and CeTTa.tex should say
;     why with normalizedShare_not_candidatewise as the formal reason.

; ---------------------------------------------------------------------------
; 5. Zero-overhead gate
; ---------------------------------------------------------------------------

; A rule whose grade is statically Bool compiles to exactly today's match:
; no value slot, no discipline call, instruction-counter verified.  This is
; the B6-style acceptance gate and the proof-carried conservativity result
; (whereDeliver = filter+map; gradedDeliver's Bool corner = identical).
