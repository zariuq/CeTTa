; Under he_extended, binder-shaped domains are still treated literally.
(: Fact Type)
(: fact Fact)
(: WitnessOf (-> $p Type))
(: remember (-> (: $p Fact) (WitnessOf $p)))
!(assertEqualToResult
  (get-type (remember fact))
  ())
