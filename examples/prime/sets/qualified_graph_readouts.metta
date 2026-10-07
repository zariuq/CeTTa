; One space keeps presentations; each observation declares its readout.
(presentation loop (graph 0 ((0))))
(presentation cycle (graph 0 ((1) (0))))
(presentation scott-pair (graph 1 ((0) (0 1))))
!(match &self (presentation loop $g)
   (match &self (presentation cycle $h)
     (let $answer (set:interpret finite-graph aczel (equal $g $h))
       (LoopAndCycle aczel $answer))))
!(match &self (presentation loop $g)
   (match &self (presentation cycle $h)
     (let $answer (set:interpret finite-graph presentation (equal $g $h))
       (LoopAndCycle presentation $answer))))
!(match &self (presentation loop $g)
   (match &self (presentation scott-pair $h)
     (let $answer (set:interpret finite-graph aczel (equal $g $h))
       (LoopAndScottPair aczel $answer))))
!(match &self (presentation loop $g)
   (match &self (presentation scott-pair $h)
     (let $answer (set:interpret finite-graph scott-canonical (equal $g $h))
       (LoopAndScottPair scott-canonical $answer))))
; A canonical quotient removes duplicate material members and keeps an
; authored representative, without changing the retained successor rows.
!(set:interpret finite-graph scott-canonical (members (graph 2 (() () (1)))))
!(set:interpret finite-graph presentation (occurrence-count (graph 0 ((1 1) ()))))
!(set:interpret finite-graph aczel (members (graph 0 ((1 1) ()))))
; Membership, self-membership and the domain of the well-founded reading.
!(set:interpret finite-graph aczel (member (graph 0 ((0))) (graph 0 ((0)))))
!(set:interpret finite-graph scott-canonical (member (graph 0 ((0))) (graph 1 ((0) (0 1)))))
!(set:interpret finite-graph foundation (eligible (graph 0 ((0)))))
!(set:interpret finite-graph foundation (eligible (graph 0 (() (1)))))
!(car-atom (try (set:interpret finite-graph foundation (value (graph 0 ((0)))))))
; Equal origin values are different occurrence receipts.
(observation (observed-graph (graph 0 ((0))) (returned 0) 7 (4 4)))
!(match &self (observation $o) (set:interpret finite-graph observed (origin-at $o 0)))
!(match &self (observation $o) (set:interpret finite-graph observed (origin-at $o 1)))
!(set:interpret finite-graph observed
   (equal (observed-graph (graph 0 (())) (returned 0) 7 (4))
          (observed-graph (graph 0 (())) (faulted 0) 7 (4))))
; Budget exhaustion is an incomplete observation, not an unequal pair.
!(car-atom (try (set:interpret finite-graph aczel
   (equal (graph 0 ((0))) (graph 0 ((1) (0)))) 0)))
