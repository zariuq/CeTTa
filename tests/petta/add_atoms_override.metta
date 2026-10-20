; An authored equation remains authoritative; native batching is disabled.
(= (add-atoms $space $items) (OverriddenAddAtomsV1 $space $items))
!(let $space (new-space)
   (case (add-atoms $space ((row 1)))
     (((OverriddenAddAtomsV1 $actual ((row 1))) override)
      ($other WRONG))))
