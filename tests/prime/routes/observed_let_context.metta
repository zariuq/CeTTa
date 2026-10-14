; A stateless observation is bound in the caller's existing Need world.
; Pending caller arguments remain usable after folding or collapsing.
(= (context:fold $prior)
   (let $items (fold (superpose (A B)) () $acc $next (cons-atom $next $acc))
     (Result $prior $items)))
!(context:fold (Prior 7))
(= (context:collapse $prior)
   (let $items (collapse (superpose (A B))) (Result $prior $items)))
!(context:collapse (Prior 7))
(= (context:prior) (Prior 7))
!(context:fold (context:prior))
!(context:collapse (context:prior))
(= (context:fold-with-used $prior)
  (let $used $prior
    (let $items (fold (superpose (A B)) () $acc $next (cons-atom $next $acc))
      (Result $used $items))))
!(context:fold-with-used (context:prior))
