(= (prime-need:region-ground $x)
   (let $promise (delay (+ $x 1))
     (force $promise)))

(= (prime-need:region-carry $x)
   (delay (+ $x 1)))
