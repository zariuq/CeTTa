!(bind! &kb (new-space))
!(add-atom &kb (ConceptProp punctual shows-up-on-time 1.0))
!(add-atom &kb
   (Sentence
     ((Inheritance alice (IntSet shows-up-on-time)) (stv 0.8 0.9))
     (receipt-1)))

(= (intensional-evidence $entity $concept)
   (collapse
     (match &kb
            (ConceptProp $concept $property $weight)
            (match &kb
                   (Sentence
                     ((Inheritance $entity (IntSet $property)) $tv)
                     $receipt)
                   ($tv $receipt $weight)))))

!(intensional-evidence alice punctual)
!(intensional-evidence bob punctual)
