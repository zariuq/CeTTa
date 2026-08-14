; An undeclared producer yields either one Number or no result.  The pinned
; strict checker withholds an inferred output type in this situation, even
; though the value-producing branch is a Number.  This pins that v2 behavior;
; a later policy may make a distinct, explicit choice.

(= (maybe-number $flag)
   (if $flag 1 (empty)))

(: accept-number (-> Number Bool))
(= (accept-number $value) True)

(: use-maybe (-[semidet]-> Bool Bool))
(= (use-maybe $flag)
   (accept-number (maybe-number $flag)))

!(test (use-maybe True) True)
!(test (collapse (use-maybe False)) ())
