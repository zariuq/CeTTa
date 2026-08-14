; A disjoint typed match-pattern candidate is permitted while the ambiguous
; variable is not consumed.  The match itself decides reachability at runtime.

(: disjoint-union-in-match (-> (| Number String) Bool))
(= (disjoint-union-in-match $value)
   (match &probe-space (: $value Bool) True))
