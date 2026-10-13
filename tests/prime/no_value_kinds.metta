; No-values of different kinds stay apart in Prime.  Each line names a kind.
(= (head-of (Cons $x $xs)) $x)
(= (head-or-none Nil) (empty))
(= (head-opt Nil) None)
(= (checked-head Nil) (Error (checked-head Nil) empty-list))
(= (tail-of (Cons $x $xs)) $xs)
(= (done Nil) ())
(= (empty-of Nil) Empty)
(= (myid $x) $x)

; zero answers: no answer, which reifies as the unit datum
!(head-or-none Nil)
!(collapse (head-or-none Nil))
; the unit datum: one answer
!(done Nil)
!(collapse (done Nil))
; an empty collection: one answer, a list with nothing in it
!(tail-of (Cons z Nil))
; an error value: one answer, and the run continues
!(checked-head Nil)
; an inert call: retained, one answer
!(head-of Nil)
!(collapse (head-of Nil))
; None: one answer of an option type
!(head-opt Nil)
; the datum Empty: one answer, and an identity function returns it
!(empty-of Nil)
!(myid Empty)
!(collapse (myid Empty))

; Distinct kinds are distinct observations.
!(== (collapse (empty-of Nil)) (collapse (head-or-none Nil)))
!(== (collapse (head-of Nil)) (collapse (head-or-none Nil)))
!(== (head-opt Nil) (empty-of Nil))
!(== (done Nil) (tail-of (Cons z Nil)))
!(== (collapse (empty)) (collapse (done Nil)))
!(== (collapse (empty)) (done Nil))

; An error alternative beside a success is kept: success-only is an explicit
; observation, not a normalization of the evaluator.
(= (g) True)
(= (g) (and True True True))
!(g)
!(collapse (g))
