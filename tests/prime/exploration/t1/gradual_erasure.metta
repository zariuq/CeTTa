; Annotation erasure.  Each program is written twice: with its type
; annotation, and with the annotation erased, as the graduality gate
; (test-prime-internal-graduality) erases the annotations of its typed
; fixtures.  Erasing an annotation never changes an answer: the annotated
; program computes what its erasure computes, or stops with a run-time error
; at the annotation, and never computes something else.
;
; A run is read through collapse, so that a run-time error is an answer
; like any other, and the verdicts below inspect it without evaluating it.

(= (verdict $answers)
   (unify $answers ((Error $call $why)) run-time-error $answers))
(= (agrees-or-fails $annotated $erased)
   (unify $annotated ((Error $call $why)) fails
     (if (== $annotated $erased) agrees differs)))

; (lam x:Int. x + 1) 2
(: successor (-> Number Number))
(= (successor $x) (+ $x 1))
(= (successor-erased $x) (+ $x 1))
; (lam x:Bool. x) 2, with the annotation wrong
(: identity-on-bool (-> Bool Bool))
(= (identity-on-bool $x) $x)
(= (identity-on-bool-erased $x) $x)
; the identity, annotated Bool -> Int, applied to true
(: bool-to-number (-> Bool Number))
(= (bool-to-number $x) $x)
(= (bool-to-number-erased $x) $x)

; 1. The successor computes 3, annotated or not (Controls.annotatedSuccessor_runs,
;    Controls.erasedSuccessor_runs), so its annotation changes nothing
;    (erase_preserves_num).
!(collapse (successor 2))
!(collapse (successor-erased 2))
!(agrees-or-fails (collapse (successor 2)) (collapse (successor-erased 2)))
; 2. A wrong annotation fails loudly: erased, the program computes 2; annotated,
;    it stops at the argument (Controls.wrong_annotation_fails), and it never
;    computes something else (annotated_agrees_or_fails).
!(collapse (identity-on-bool-erased 2))
!(verdict (collapse (identity-on-bool 2)))
!(agrees-or-fails (collapse (identity-on-bool 2)) (collapse (identity-on-bool-erased 2)))
; 3. The result is checked too: erased, the identity returns true
;    (Controls.identity_runs); annotated, it stops at the result
;    (Controls.codomain_error).
!(collapse (bool-to-number-erased True))
!(verdict (collapse (bool-to-number True)))
