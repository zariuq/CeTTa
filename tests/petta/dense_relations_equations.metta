; Relations kept as bits or tables are read inside equations as at the top
; level: a match there lends the relation's facts after the rows, so the
; relation stays, and every way of reading answers alike: columns fixed or
; bound by an earlier match, joins of two and three columns, the same
; variable in two columns, and matches whose answers add to and remove from
; the relation they read, which answer the facts there were when they began.
!(bind! &kb (new-space))
(= (fill $k $n)
   (if (>= $k $n) done
       (let* (($k1 (+ $k 1))
              ($j (% (* $k 7) 13))
              ($t (% $k 3))
              ($a (add-atom &kb (edge $k $j)))
              ($b (add-atom &kb (kv $k color $j)))
              ($c (add-atom &kb (kv $k tone $t))))
         (fill $k1 $n))))
!(fill 0 40)
; one column fixed
(= (colors-of $k) (match &kb (kv $k color $v) $v))
!(collapse (colors-of 5))
(= (with-color $v) (match &kb (kv $k color $v) $k))
!(collapse (with-color 9))
; joins: two columns with two, then two with three
(= (two-hop $x) (match &kb (edge $x $y) (match &kb (edge $y $z) $z)))
!(collapse (two-hop 3))
(= (tone-of-target $x) (match &kb (edge $x $y) (match &kb (kv $y tone $t) $t)))
!(collapse (tone-of-target 4))
(= (same-tone $x) (match &kb (kv $x tone $t) (match &kb (kv $y tone $t) $y)))
!(size-atom (collapse (same-tone 7)))
; the same variable in two columns
(= (loops) (match &kb (edge $x $x) $x))
!(collapse (loops))
; a column the relation has no fact for
(= (none-of $k) (match &kb (kv $k smell $v) $v))
!(collapse (none-of 5))
; additions to the relation a match reads: its answers are the facts there
; were when it began
(= (copy-edges)
   (match &kb (edge $x $y) (let $x1 (+ $x 100) (add-atom &kb (edge $x1 $y)))))
!(size-atom (collapse (copy-edges)))
!(size-atom (collapse (match &kb (edge $x $y) t)))
; removals from it
(= (drop-tones)
   (match &kb (kv $k tone 0) (remove-atom &kb (kv $k tone 0))))
!(size-atom (collapse (drop-tones)))
!(collapse (match &kb (kv $k tone 0) $k))
!(size-atom (collapse (match &kb (kv $k tone $t) $k)))
; a fact with a variable stays a row, and is read with the relation
!(add-atom &kb (kv 200 color $any))
(= (colored $k) (match &kb (kv $k color $v) yes))
!(collapse (colored 200))
!(size-atom (collapse (match &kb (kv $k color $v) $k)))
