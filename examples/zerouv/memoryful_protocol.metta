(= waiting proposed)
(= proposed checked)
(= checked accepted)

(= (review draft waiting) (choose checking proposed))
(= (review checking proposed) (choose deciding checked))
(= (review deciding checked) (choose archived accepted))

(! (follow review draft (s (s (s z))) waiting))
