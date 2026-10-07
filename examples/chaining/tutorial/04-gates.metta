!(include backward-core)
!(include graph-facts)
!(import! &self pat)

; A gate can prune an application without refining the goal being inspected.
; This policy deliberately excludes a premise, so it sacrifices completeness.
(= (avoid-bc $remaining $goal)
   (if (> $remaining 0)
       (pat:unify% (edge bob carol) $goal False True)
       False))

!(collapse (bc depth-gate 4 (reachable alice dana)))
!(collapse (bc avoid-bc 4 (reachable alice dana)))
!(collapse (bc avoid-bc 2 (reachable alice bob)))
