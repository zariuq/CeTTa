; Chapter 6: lazy memoization for repeated subgoals
;
; This is a building block for tabling, not a complete table protocol.  A full
; chainer still needs call keys, waiter management, completion, and cycle rules.
; The context contributes one useful law: a posed subgoal is searched at most
; once per branch and every consumer receives the same chosen proof occurrence.

(= (ctx-table:search $goal)
   (superpose
     ((proof route-a $goal)
      (proof route-b $goal))))

(= (ctx-table:verify $proof)
   (verified $proof))

(= (ctx-table:compress $proof)
   (compressed $proof))

; Two direct consumers reuse one memoized proof choice.
!(let $memo
     (ctx:bind ctx:empty @(subgoal theorem)
       (ctx-table:search theorem))
   (Pair
     (ctx:get $memo @(subgoal theorem))
     (ctx:get $memo @(subgoal theorem))))

; Different downstream algorithms still agree on which proof was chosen.
!(let $memo
     (ctx:bind ctx:empty @(subgoal theorem)
       (ctx-table:search theorem))
   (Pair
     (ctx-table:verify
       (ctx:get $memo @(subgoal theorem)))
     (ctx-table:compress
       (ctx:get $memo @(subgoal theorem)))))

; Two separately posed subgoals are independent, even with equal source text.
!(let $left
     (ctx:bind ctx:empty @(subgoal theorem)
       (ctx-table:search theorem))
   (let $right
       (ctx:bind ctx:empty @(subgoal theorem)
         (ctx-table:search theorem))
     (Pair
       (ctx:get $left @(subgoal theorem))
       (ctx:get $right @(subgoal theorem)))))

; An unposed subgoal remains explicit data rather than becoming a fault.
!(ctx:get ctx:empty @(subgoal missing-theorem))
