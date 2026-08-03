(= (living garfield) True)
(= (being garfield) True)
(= (small garfield) True)

(= (being roomba) True)
(= (small roomba) True)

(= (living cat42) True)
(= (being cat42) True)
(= (small cat42) True)

(= (only $constraint $value)
   (let $_ $constraint $value))

(= (animal $value)
   (only ((living $value) (being $value)) $value))

(= (cat (animal $value))
   (only (small $value) $value))

!(test (msort (collapse (cat $value)))
       (cat42 garfield))
