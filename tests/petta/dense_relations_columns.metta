; Relations of one, three and five columns of atoms equal only to
; themselves may be kept as tables of tuples.  Every way of reading them
; answers alike whether they are: matches with columns fixed, bound by an
; earlier pattern or free, the same variable in two columns, counts,
; removals of present and absent facts, copies of a fact in a bag, a set
; that holds each fact once, facts of the same head with other columns,
; which stay rows, and a listing of the whole space.
!(bind! &bag (new-space))
!(bind! &set (new-space set))
(= (fill $space $k $n)
   (if (>= $k $n) done
       (let* (($k1 (+ $k 1))
              ($j (% (* $k 7) 13))
              ($m (% $k 3))
              ($a (add-atom $space (node $k)))
              ($b (add-atom $space (kv $k color $j)))
              ($c (add-atom $space (kv $k tone $m)))
              ($d (add-atom $space (row5 $k $j $m "s" $k1))))
         (fill $space $k1 $n))))
!(fill &bag 0 40)
!(fill &set 0 40)
; a copy of a fact the bag holds already, and the same added to the set
!(add-atom &bag (kv 3 color 8))
!(add-atom &set (kv 3 color 8))
!(size-atom (collapse (match &bag (kv $x $p $v) $x)))
!(size-atom (collapse (match &set (kv $x $p $v) $x)))
; one column
!(size-atom (collapse (match &bag (node $x) $x)))
!(collapse (match &bag (node 7) yes))
!(collapse (match &bag (node 70) yes))
; three columns: one fixed, two, all three
!(collapse (match &bag (kv 5 $p $v) ($p $v)))
!(size-atom (collapse (match &bag (kv $x color $v) $x)))
!(collapse (match &bag (kv $x tone 2) $x))
!(collapse (match &bag (kv 5 color $v) $v))
!(collapse (match &bag (kv 5 color 9) yes))
!(collapse (match &bag (kv 5 color 10) yes))
; conjunctions binding columns from an earlier pattern
!(size-atom (collapse (match &bag (, (kv $x color $c) (kv $c tone $s)) ($x $c $s))))
!(collapse (match &bag (, (node $x) (kv $x tone 0) (kv $x color 0)) $x))
; five columns, with a string, and the same variable in two columns
!(collapse (match &bag (row5 $k $j 1 "s" $n) ($k $j $n)))
!(collapse (match &bag (row5 $k $k $m $s $n) ($k $m)))
; removals: a fact held once, a copy, a fact never held
!(remove-atom &bag (kv 3 color 8))
!(size-atom (collapse (match &bag (kv 3 color 8) yes)))
!(remove-atom &bag (kv 3 color 8))
!(size-atom (collapse (match &bag (kv 3 color 8) yes)))
!(remove-atom &bag (kv 100 color 200))
!(remove-atom &set (kv 3 color 8))
!(size-atom (collapse (match &set (kv 3 color 8) yes)))
!(remove-atom &bag (node 7))
!(collapse (match &bag (node 7) yes))
; facts of the same head with other columns stay rows and still match
!(add-atom &bag (kv 1 2))
!(add-atom &bag (kv 1 2 3 4))
!(collapse (match &bag (kv 1 $y) $y))
!(collapse (match &bag (kv 1 $a $b $c) ($a $b $c)))
!(size-atom (collapse (match &bag (kv 1 $a $b) ($a $b))))
; a float is no column a table holds: its fact stays a row
!(add-atom &bag (kv 2 weight 1.5))
!(collapse (match &bag (kv 2 weight $w) $w))
; a variable head reads every relation of its columns
!(size-atom (collapse (match &bag ($h 5 $p $v) ($h $p $v))))
; the whole space, as a bag
!(size-atom (collapse (get-atoms &bag)))
!(size-atom (collapse (get-atoms &set)))
; a fact with a variable stays a row and still matches
!(add-atom &bag (kv 50 color $any))
!(collapse (match &bag (kv 50 color $w) $w))
!(size-atom (collapse (match &bag (kv $x $p $v) $x)))
