;; Row 09 — PLN premise-selector inference core (CeTTa-portable).
;; Source strategy: PeTTa's PLN premise-selector demo + PLN library
;;   (github.com/patham9/PeTTa : demos/pln_premise_selector.metta, lib/lib_pln.metta).
;; The original selector reads evidence from TSV via SWI-Prolog FFI
;; (import_prolog_function/consult) — that is I/O plumbing, NOT the inference.
;; This row keeps the exact PLN math (Truth_ModusPonens chains feature evidence,
;; Truth_Revision aggregates) but inlines the evidence as atoms (reproducible, no FFI),
;; so the inference CORE (not the end-to-end selector) runs on stock HE arithmetic.
;;
;; MINIMAL CeTTa REQUIREMENT: none beyond stock HE arithmetic (+ - * /). The PLN core
;; below is the whole "library" — ~8 lines, transcribed from lib_pln.metta's formulas
;; (PLN book Sec. 5.7.1 Modus Ponens, Sec. 5.10.2 Heuristic Revision). Runs on CeTTa
;; --lang he AND PeTTa; both return the same (Result <axiom> (stv s c)) to display
;; precision (verify the strength/confidence values, not just result counts).

;; ---- Minimal portable PLN core (inlined from lib_pln.metta) ----
(= (c2w $c) (/ $c (- 1 $c)))            ; confidence -> evidence weight (0<=c<1)
(= (w2c $w) (/ $w (+ $w 1)))            ; weight -> confidence
(= (Truth_ModusPonens (stv $f1 $c1) (stv $f2 $c2))
   (stv (+ (* $f1 $f2) (* 0.02 (- 1 $f1))) (* $c1 $c2)))
(= (Truth_Revision (stv $f1 $c1) (stv $f2 $c2))
   (let* (($w1 (c2w $c1)) ($w2 (c2w $c2)) ($w (+ $w1 $w2))
          ($f (/ (+ (* $w1 $f1) (* $w2 $f2)) $w)) ($c (w2c $w)))
     (stv $f $c)))

;; ---- Feature -> usefulness implication TVs (rule base) ----
(= (rule-tv feat-arith)     (stv 0.85 0.70))
(= (rule-tv feat-recursion) (stv 0.75 0.60))
(= (rule-tv feat-equality)  (stv 0.65 0.55))

;; ---- Observed features per candidate axiom (observation TVs) ----
(= (axiom-feature thmA feat-arith)    (stv 0.90 0.90))
(= (axiom-feature thmA feat-equality) (stv 0.80 0.80))
(= (axiom-feature thmB feat-recursion)(stv 0.70 0.85))
(= (axiom-feature thmB feat-equality) (stv 0.60 0.75))

;; ---- Inference: per-feature contribution = ModusPonens(observation, rule) ----
(= (contrib $ax $feat) (Truth_ModusPonens (axiom-feature $ax $feat) (rule-tv $feat)))

;; ---- Select: revise the feature contributions into one usefulness TV ----
!(let* (($c1 (contrib thmA feat-arith))
        ($c2 (contrib thmA feat-equality)))
   (Result thmA (Truth_Revision $c1 $c2)))
!(let* (($c1 (contrib thmB feat-recursion))
        ($c2 (contrib thmB feat-equality)))
   (Result thmB (Truth_Revision $c1 $c2)))
