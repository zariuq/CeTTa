; Stored presentations can be inspected by distinct declared observers.
!(bind! &graphs (new-space))
!(add-atom &graphs (presentation loop (graph 0 ((0)))))
!(add-atom &graphs (presentation cycle (graph 0 ((1) (0)))))
!(match &graphs (presentation loop $g)
   (match &graphs (presentation cycle $h)
     (set:interpret finite-graph aczel (equal $g $h))))
!(match &graphs (presentation loop $g)
   (match &graphs (presentation cycle $h)
     (set:interpret finite-graph presentation (equal $g $h))))
!(match &graphs (presentation loop $g)
   (set:interpret finite-graph foundation (eligible $g)))
!(add-atom &graphs
   (observation (observed-graph (graph 0 ((0))) (returned 0) 7 (4 4))))
; Equal origin values have distinct positions and retain their source.
!(match &graphs (observation $o)
   (set:interpret finite-graph observed (origin-at $o 0)))
!(match &graphs (observation $o)
   (set:interpret finite-graph observed (origin-at $o 1)))
