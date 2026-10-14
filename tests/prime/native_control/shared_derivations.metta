; Two derivations reuse one lazy choice; four take independent choices.
(= (leaf) (D a 1))
(= (leaf) (D b 2))
(= (combine (D $left $lcost) (D $right $rcost))
   (D (Pair $left $right) (+ $lcost $rcost)))
(= (graph) (let $shared (leaf) (combine $shared $shared)))
(= (graph) (combine (leaf) (leaf)))
(= (graph) (D Direct 5))
!(select 7 (search-policy (control WO
   (best (D $tree $cost) $cost) (budget 20000))) (graph))
