(: has-hit (-> Atom %Undefined%))
(= (has-hit $items)
   (case $items
     ((End False)
      ((Link (Hit $value) $rest) True)
      ((Link $other $rest) (has-hit $rest)))))

; A nonlinear designated pattern is not the admitted constructor test.
(: has-equal-pair (-> Atom %Undefined%))
(= (has-equal-pair $items)
   (case $items
     ((End False)
      ((Link (PairHit $value $value) $rest) True)
      ((Link $other $rest) (has-equal-pair $rest)))))

!(has-hit (Link plain (Link (Hit yes) End)))
!(has-hit (Link plain End))
!(has-equal-pair (Link (PairHit a b) End))
!(has-equal-pair (Link (PairHit a a) End))
!(once (has-hit (Link $open End)))
