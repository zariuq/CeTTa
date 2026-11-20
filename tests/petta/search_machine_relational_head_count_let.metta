; An equation whose head argument structural matching cannot discharge, such
; as (cons $p $rest) against the list (1), is opened by solving that argument
; against the call's.  Its body then runs like the body of a structurally
; matched head.  A let whose binder no use reads runs its collapse for the
; answers' effects alone, and a let that only counts them reads the count.
; Collapse of no answers is (), so each body answers once.  The rows cross
; three head shapes, (cons $p $rest), ($p) and (a $p), with inner bodies that
; fail through their producer or through an argument, and with inner bodies
; that do not fail that way.

(= (identity-of $x) $x)
(= (only-a a) a)

(= (inner-empty $p) (let $c (empty) $p))
(= (inner-argument $p) (let $c (identity-of (empty)) $p))
(= (inner-value $p) (let $c 5 $p))
(= (inner-choice $p) (let $c (superpose (1 2)) $p))
(= (inner-bare $p) (empty))
(= (inner-if $p) (if (== $p 2) $p (empty)))
(= (inner-unmatched $p) (let $c (only-a $p) $p))

(= (cons-empty (cons $p $rest)) (let $u (collapse (inner-empty $p)) done))
(= (cons-argument (cons $p $rest)) (let $u (collapse (inner-argument $p)) done))
(= (cons-value (cons $p $rest)) (let $u (collapse (inner-value $p)) done))
(= (cons-choice (cons $p $rest)) (let $u (collapse (inner-choice $p)) done))
(= (cons-bare (cons $p $rest)) (let $u (collapse (inner-bare $p)) done))
(= (cons-if (cons $p $rest)) (let $u (collapse (inner-if $p)) done))
(= (cons-unmatched (cons $p $rest)) (let $u (collapse (inner-unmatched $p)) done))

(= (list-empty ($p)) (let $u (collapse (inner-empty $p)) done))
(= (list-argument ($p)) (let $u (collapse (inner-argument $p)) done))
(= (list-value ($p)) (let $u (collapse (inner-value $p)) done))
(= (list-choice ($p)) (let $u (collapse (inner-choice $p)) done))
(= (list-bare ($p)) (let $u (collapse (inner-bare $p)) done))
(= (list-if ($p)) (let $u (collapse (inner-if $p)) done))
(= (list-unmatched ($p)) (let $u (collapse (inner-unmatched $p)) done))

(= (tag-empty (a $p)) (let $u (collapse (inner-empty $p)) done))
(= (tag-argument (a $p)) (let $u (collapse (inner-argument $p)) done))
(= (tag-value (a $p)) (let $u (collapse (inner-value $p)) done))
(= (tag-choice (a $p)) (let $u (collapse (inner-choice $p)) done))
(= (tag-bare (a $p)) (let $u (collapse (inner-bare $p)) done))
(= (tag-if (a $p)) (let $u (collapse (inner-if $p)) done))
(= (tag-unmatched (a $p)) (let $u (collapse (inner-unmatched $p)) done))

; The count of the answers, read by a let whose only use counts them.
(= (count-empty (cons $p $rest)) (let $u (collapse (inner-empty $p)) (size-atom $u)))
(= (count-value (cons $p $rest)) (let $u (collapse (inner-value $p)) (size-atom $u)))
(= (count-choice (cons $p $rest)) (let $u (collapse (inner-choice $p)) (size-atom $u)))

; An anonymous binder, and a walk that opens one equation per element.
(= (anonymous-empty (cons $p $rest)) (let $_ (collapse (inner-empty $p)) done))
(= (walk ()) ())
(= (walk (cons $p $rest)) (let $_ (collapse (inner-empty $p)) (walk $rest)))

!(ConsHeads (cons-empty (1)) (cons-argument (1)) (cons-value (1)) (cons-choice (1))
  (cons-bare (1)) (cons-if (1)) (cons-unmatched (1)))
!(ListHeads (list-empty (1)) (list-argument (1)) (list-value (1)) (list-choice (1))
  (list-bare (1)) (list-if (1)) (list-unmatched (1)))
!(TagHeads (tag-empty (a 1)) (tag-argument (a 1)) (tag-value (a 1)) (tag-choice (a 1))
  (tag-bare (a 1)) (tag-if (a 1)) (tag-unmatched (a 1)))
!(Counts (count-empty (1)) (count-value (1)) (count-choice (1)))
!(Anonymous (anonymous-empty (1)))
!(Walk (walk (1 2 3)))
!(Collected (collapse (cons-empty (1))))
; A parallel branch reads the program through its revision view.
!(Branches (collapse (hyperpose ((cons-empty (1)) (cons-argument (2 3))))))
