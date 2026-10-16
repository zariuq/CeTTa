; Each iteration creates an empty space that nothing keeps.
(= (make $n) (if (== $n 0) done (let $s (new-space) (make (- $n 1)))))
!(make 100)
