; Repeated continuation-slot writes under two live, ordered alternatives.
(= (checkpoint-drain $n $tag)
   (if (> $n 0)
       (let $prior (- $n 1)
         (let $again (+ $prior 1)
           (if (== $again $n) (checkpoint-drain $prior $tag) Empty)))
       $tag))
(= (checkpoint-writes $n)
   (chain (superpose (first second)) $side
     (chain (superpose (10 20)) $number
       (chain (checkpoint-drain $n $side) $tag
         (result $side $number $tag)))))
!(checkpoint-writes 64)
