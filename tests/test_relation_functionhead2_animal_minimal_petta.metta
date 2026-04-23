; Minimal pre-extension witness extracted from functionhead2.

(= (living garfield) True)
(= (being garfield) True)
(= (being roomba) True)
(= (living cat42) True)
(= (being cat42) True)

(= (only $C $X)
   (let $constraint $C $X))

(= (animal $X)
   (only ((living $X) (being $X)) $X))

!(test (msort (collapse (animal $X)))
       (cat42 garfield))
