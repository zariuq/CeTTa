(= (plus Z $right) $right)
(= (plus (S $left) $right)
   (S (plus $left $right)))

; The requested constructor shape bounds the recursive inverse call.
!(let (plus $left (S Z))
      (S (S (S (S Z))))
      $left)

; Both inputs may remain relational; declaration order determines the bag.
!(collapse
    (let (plus $left $right)
         (S (S Z))
         ($left $right)))

; A fixed recursive call cannot unify its constructor result with a symbol.
!(collapse
    (let (plus (S Z) Z)
         impossible
         present))
