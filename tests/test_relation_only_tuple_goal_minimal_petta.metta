; Minimal tuple-goal witness for Petta's only surface.

(= (living garfield) True)
(= (being garfield) True)
(= (being roomba) True)
(= (living cat42) True)
(= (being cat42) True)

(= (only $C $X)
   (let $constraint $C $X))

!(test (msort (collapse (only ((living $X) (being $X)) $X)))
       (cat42 garfield))
