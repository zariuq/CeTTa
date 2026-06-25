!(import! &self ../../lib/lib_wmpln)

; Runnable shadow for the ProbHOL / planner / regime-mixture overlay boundary.
;
; This mirrors the Lean theorem surfaces in:
; - PLNProbHOLPlannerBridge.lean
; - PLNProbHOLPlannerBridgeRegression.lean
; - PLNRegimeMixtureBenchmarkBridge.lean
; - PLNRegimeMixtureRegression.lean
;
; The lesson is intentionally modest: planner and regime-mixture consumers carry
; a semantic ProbHOL value and reason about reveal/approximation tradeoffs. They
; do not become the canonical HOL semantics or a global belief oracle.
;
; Hand-derived toy instance:
;   weights      = (1/2, 1/4, 1/4)
;   branch vals  = (0.8, 0.4, 0.0)
;   mixture      = .5*.8 + .25*.4 + .25*0 = .5
;   variance     = .5*(.8-.5)^2 + .25*(.4-.5)^2 + .25*(0-.5)^2 = .11
;   exact risk at branch 1 predictor .8 = .25*(.4-.8)^2 + .25*(0-.8)^2 = .20
;   direct error from branch 1 predictor = |.5-.8| = .3
;   residual-mass * branch-radius = .5*.8 = .4, so .3 <= .4.

(= (absdiff $x $y) (if (< $x $y) (- $y $x) (- $x $y)))
(= (approx $x $y) (< (absdiff $x $y) 0.000001))

(= (sq $x) (* $x $x))
(= (max2 $x $y) (if (< $x $y) $y $x))

(= (mixture-value3 $w1 $w2 $w3 $q1 $q2 $q3)
   (+ (+ (* $w1 $q1) (* $w2 $q2)) (* $w3 $q3)))

(= (planner-carried-value3 $w1 $w2 $w3 $q1 $q2 $q3)
   (mixture-value3 $w1 $w2 $w3 $q1 $q2 $q3))

(= (expected-squared-loss3 $w1 $w2 $w3 $q1 $q2 $q3 $x)
   (+ (+ (* $w1 (sq (- $q1 $x)))
         (* $w2 (sq (- $q2 $x))))
      (* $w3 (sq (- $q3 $x)))))

(= (mixture-variance3 $w1 $w2 $w3 $q1 $q2 $q3)
   (expected-squared-loss3
     $w1 $w2 $w3 $q1 $q2 $q3
     (mixture-value3 $w1 $w2 $w3 $q1 $q2 $q3)))

(= (reveal-gain3 $w1 $w2 $w3 $q1 $q2 $q3 $cost)
   (- (mixture-variance3 $w1 $w2 $w3 $q1 $q2 $q3) $cost))

(= (direct-error-branch1 $w1 $w2 $w3 $q1 $q2 $q3)
   (absdiff (mixture-value3 $w1 $w2 $w3 $q1 $q2 $q3) $q1))

(= (direct-bound-branch1 $w1 $w2 $w3 $q1 $q2 $q3)
   (* (+ $w2 $w3) (max2 (absdiff $q2 $q1) (absdiff $q3 $q1))))

; The planner shadow tracks the intended benchmark process/sample, not arbitrary
; expanded latent-regime internals. This is the runnable boundary condition for
; the Lean negative theorem that the planner day does not track expanded samples.
(= (tracks-benchmark-process? WM-PLN-BENCHMARK) True)
(= (tracks-expanded-latent-sample? WM-PLN-BENCHMARK) False)

; Positive overlay: the semantic mixture value is carried into the planner layer.
!(assertEqual
  (approx (mixture-value3 0.5 0.25 0.25 0.8 0.4 0.0) 0.5)
  True)
!(assertEqual
  (approx (planner-carried-value3 0.5 0.25 0.25 0.8 0.4 0.0) 0.5)
  True)

; Negative overlay boundary: the planner is not pretending the first branch is
; the whole semantic mixture.
!(assertEqual
  (approx (planner-carried-value3 0.5 0.25 0.25 0.8 0.4 0.0) 0.8)
  False)

; Mixture variance and branch-predictor risk are hand-derived above.
!(assertEqual
  (approx (mixture-variance3 0.5 0.25 0.25 0.8 0.4 0.0) 0.11)
  True)
!(assertEqual
  (approx (expected-squared-loss3 0.5 0.25 0.25 0.8 0.4 0.0 0.8) 0.20)
  True)
!(assertEqual
  (<= (mixture-variance3 0.5 0.25 0.25 0.8 0.4 0.0)
      (expected-squared-loss3 0.5 0.25 0.25 0.8 0.4 0.0 0.8))
  True)

; Reveal is useful when cost is below variance, and not useful above it.
!(assertEqual
  (> (reveal-gain3 0.5 0.25 0.25 0.8 0.4 0.0 0.0) 0.0)
  True)
!(assertEqual
  (<= (reveal-gain3 0.5 0.25 0.25 0.8 0.4 0.0 0.2) 0.0)
  True)

; Direct approximation: the exact branch error is bounded by residual mass times
; the branch radius, matching the Lean direct-approximation bridge shape.
!(assertEqual
  (approx (direct-error-branch1 0.5 0.25 0.25 0.8 0.4 0.0) 0.3)
  True)
!(assertEqual
  (approx (direct-bound-branch1 0.5 0.25 0.25 0.8 0.4 0.0) 0.4)
  True)
!(assertEqual
  (<= (direct-error-branch1 0.5 0.25 0.25 0.8 0.4 0.0)
      (direct-bound-branch1 0.5 0.25 0.25 0.8 0.4 0.0))
  True)

; Boundary checks: the planner tracks the benchmark process, but not the latent
; expanded sample as though it were a global belief oracle.
!(assertEqual (tracks-benchmark-process? WM-PLN-BENCHMARK) True)
!(assertEqual (tracks-expanded-latent-sample? WM-PLN-BENCHMARK) False)
