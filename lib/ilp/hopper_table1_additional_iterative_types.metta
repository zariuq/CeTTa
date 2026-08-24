; Typed vocabulary for three Hopper programs realized by iteration.
; Each iteration step remains an ordinary proof-relevant relation.

(: hopper:rotate-n:step
  (-> (source : (list hopper:nat))
      (target : (list hopper:nat)) (u 0)))
(: hopper:rotate-n:step-proof
  (-> (source : (list hopper:nat)) (head : hopper:nat)
      (tail : (list hopper:nat))
      (target : (list hopper:nat))
      (head-evidence : (rel:list:head hopper:nat source head))
      (tail-evidence : (rel:list:tail hopper:nat source tail))
      (snoc-evidence : (rel:list:snoc hopper:nat tail head target))
      (hopper:rotate-n:step source target)))
(: hopper:rotate-n:f
  (-> (count : hopper:nat) (source : (list hopper:nat))
      (target : (list hopper:nat)) (u 0)))
(: hopper:rotate-n:proof
  (-> (count : hopper:nat) (source : (list hopper:nat))
      (target : (list hopper:nat))
      (evidence :
        (rel:iterate (list hopper:nat) hopper:nat
          hopper:rotate-n:step hopper:nat:pred hopper:nat:n0
          source count target))
      (hopper:rotate-n:f count source target)))

(: hopper:drop-last-k:step
  (-> (source : (list (list hopper:atom)))
      (target : (list (list hopper:atom))) (u 0)))
(: hopper:drop-last-k:step-proof
  (-> (source : (list (list hopper:atom)))
      (target : (list (list hopper:atom)))
      (evidence :
        (map-rel (list hopper:atom) (list hopper:atom)
          hopper:drop-last:element source target))
      (hopper:drop-last-k:step source target)))
(: hopper:drop-last-k:f
  (-> (count : hopper:nat)
      (source : (list (list hopper:atom)))
      (target : (list (list hopper:atom))) (u 0)))
(: hopper:drop-last-k:proof
  (-> (count : hopper:nat)
      (source : (list (list hopper:atom)))
      (target : (list (list hopper:atom)))
      (evidence :
        (rel:iterate (list (list hopper:atom)) hopper:nat
          hopper:drop-last-k:step hopper:nat:pred hopper:nat:n0
          source count target))
      (hopper:drop-last-k:f count source target)))

(: hopper:add-n:step
  (-> (source : (list hopper:nat))
      (target : (list hopper:nat)) (u 0)))
(: hopper:add-n:step-proof
  (-> (source : (list hopper:nat))
      (target : (list hopper:nat))
      (evidence :
        (map-rel hopper:nat hopper:nat hopper:nat:succ
          source target))
      (hopper:add-n:step source target)))
(: hopper:add-n:f
  (-> (count : hopper:nat) (source : (list hopper:nat))
      (target : (list hopper:nat)) (u 0)))
(: hopper:add-n:proof
  (-> (count : hopper:nat) (source : (list hopper:nat))
      (target : (list hopper:nat))
      (evidence :
        (rel:iterate (list hopper:nat) hopper:nat
          hopper:add-n:step hopper:nat:pred hopper:nat:n0
          source count target))
      (hopper:add-n:f count source target)))
