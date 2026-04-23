; Minimal goal-enumeration witness: relation-callable facts should enumerate witnesses.

(= (living garfield) True)
(= (living cat42) True)

!(test (msort (collapse (let True (living $X) $X)))
       (cat42 garfield))
