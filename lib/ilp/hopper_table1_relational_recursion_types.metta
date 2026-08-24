; Typed programs for source-authored relational recursion.
; Environments and intermediate states remain explicit evidence indices.

(: hopper:nat:pred-clamped
  (-> (later : hopper:nat) (earlier : hopper:nat) (u 0)))
(: hopper:nat:proof:pred-clamped-zero
  (hopper:nat:pred-clamped hopper:nat:n0 hopper:nat:n0))
(: hopper:nat:proof:pred-clamped-positive
  (-> (later : hopper:nat) (earlier : hopper:nat)
      (evidence : (hopper:nat:pred later earlier))
      (hopper:nat:pred-clamped later earlier)))

(: hopper:repeat-n:f
  (-> (element : (u $element-level))
      (source : (list element)) (count : hopper:nat)
      (target : (list (list element))) (u $element-level)))
(: hopper:repeat-n:proof
  (-> (element : (u $element-level))
      (source : (list element)) (count : hopper:nat)
      (target : (list (list element)))
      (evidence :
        (rel:list:repeat (list element) hopper:nat
          hopper:nat:pred hopper:nat:n0 source
          (list:nil (list element)) count target))
      (hopper:repeat-n:f element source count target)))

(: hopper:all-seq-n:successor-emit
  (-> (current : hopper:nat) (head : hopper:nat)
      (next : hopper:nat) (u 0)))
(: hopper:all-seq-n:successor-emit-proof
  (-> (current : hopper:nat) (next : hopper:nat)
      (evidence : (hopper:nat:succ current next))
      (hopper:all-seq-n:successor-emit current next next)))
(: hopper:all-seq-n:prefix
  (-> (count : hopper:nat) (values : (list hopper:nat)) (u 0)))
(: hopper:all-seq-n:prefix-proof
  (-> (count : hopper:nat) (values : (list hopper:nat))
      (evidence :
        (rel:unfold hopper:nat hopper:nat hopper:nat
          hopper:nat:pred hopper:all-seq-n:successor-emit
          hopper:nat:n0 hopper:nat:n0 count values))
      (hopper:all-seq-n:prefix count values)))
(: hopper:all-seq-n:f
  (-> (count : hopper:nat)
      (values : (list (list hopper:nat))) (u 0)))
(: hopper:all-seq-n:proof
  (-> (count : hopper:nat) (indices : (list hopper:nat))
      (values : (list (list hopper:nat)))
      (scan-evidence :
        (rel:unfold hopper:nat hopper:nat hopper:nat
          hopper:nat:pred hopper:all-seq-n:successor-emit
          hopper:nat:n0 hopper:nat:n0 count indices))
      (map-evidence :
        (map-rel hopper:nat (list hopper:nat)
          hopper:all-seq-n:prefix indices values))
      (hopper:all-seq-n:f count values)))

(: hopper:first-half:advance
  (-> (later : hopper:nat) (earlier : hopper:nat) (u 0)))
(: hopper:first-half:advance-proof
  (-> (later : hopper:nat) (middle : hopper:nat)
      (earlier : hopper:nat)
      (first : (hopper:nat:pred-clamped later middle))
      (second : (hopper:nat:pred-clamped middle earlier))
      (hopper:first-half:advance later earlier)))
(: hopper:first-half:emit
  (-> (current : (list hopper:atom)) (head : hopper:atom)
      (next : (list hopper:atom)) (u 0)))
(: hopper:first-half:emit-proof
  (-> (current : (list hopper:atom)) (head : hopper:atom)
      (next : (list hopper:atom))
      (head-evidence : (rel:list:head hopper:atom current head))
      (tail-evidence : (rel:list:tail hopper:atom current next))
      (hopper:first-half:emit current head next)))
(: hopper:first-half:f
  (-> (source : (list hopper:atom))
      (target : (list hopper:atom)) (u 0)))
(: hopper:first-half:proof
  (-> (source : (list hopper:atom)) (count : hopper:nat)
      (target : (list hopper:atom))
      (length-evidence : (hopper:length:f source count))
      (unfold-evidence :
        (rel:unfold (list hopper:atom) hopper:nat hopper:atom
          hopper:first-half:advance hopper:first-half:emit
          hopper:nat:n0 source count target))
      (hopper:first-half:f source target)))

(: hopper:nat:add-from-suc
  (-> (amount : hopper:nat) (source : hopper:nat)
      (target : hopper:nat) (u 0)))
(: hopper:nat:add-from-suc-proof
  (-> (amount : hopper:nat) (source : hopper:nat)
      (target : hopper:nat)
      (evidence :
        (rel:iterate hopper:nat hopper:nat hopper:nat:succ
          hopper:nat:pred hopper:nat:n0 source amount target))
      (hopper:nat:add-from-suc amount source target)))
(: hopper:mul-from-suc:f
  (-> (left : hopper:nat) (right : hopper:nat)
      (target : hopper:nat) (u 0)))
(: hopper:mul-from-suc:proof
  (-> (left : hopper:nat) (right : hopper:nat)
      (target : hopper:nat)
      (evidence :
        (rel:iterate-with hopper:nat hopper:nat hopper:nat
          hopper:nat:add-from-suc hopper:nat:pred hopper:nat:n0
          right hopper:nat:n0 left target))
      (hopper:mul-from-suc:f left right target)))
