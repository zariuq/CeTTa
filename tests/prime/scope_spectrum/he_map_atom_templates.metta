; HE controls (run with --lang he): HE keeps its own map-atom, which seals
; the template at the call, so a template's free names are renamed apart for
; each element.  Prime's rows C2 and C4 (tests/prime/routes/
; binding_templates.metta) differ by design.

; The template's $y is two fresh names, neither one the caller's $y.
!(let $r (map-atom (1 2) $e (Pair $e $y)) (let $y 5 (let ((Pair 1 $a) (Pair 2 $b)) $r (Pair (== $a 5) (== $a $b)))))
; The template's let refines its own renamed $y; the caller's $y stays free.
!(Pair (map-atom (1 2) $e (let $y $e (g $y))) $y)
