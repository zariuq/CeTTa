; Counts over a pattern or a conjunction count the facts the space keeps in
; relations beside its rows, and rows holding variables, which meet every
; pattern they unify with, as ordinary search does.  (A PeTTa program stores
; no bare variable; tests/he/native_count_stored_variable.metta counts one.)
(edge 1 2)
(edge 2 3)
(edge 3 4)
(edge 2 5)
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (, (edge $x $y) (edge $y $z)) 1) 0)
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (, (edge $x $y) (edge $y $z) (edge $z $w)) 1) 0)
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (edge $x $y) 2) 1)
!(size-atom (collapse (match &self (, (edge $x $y) (edge $y $z)) t)))
; A head past the threshold of rows, made by the program's own adds.
(= (fill $n)
   (if (> $n 0)
       (let* (($m (- $n 1)) ($_ (add-atom &self (link $n $m)))) (fill $m))
       done))
!(fill 4200)
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (, (link $x $y) (link $y $z)) 1) 0)
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (link $x $y) 1) 0)
; Rows of two heads, each below the count at which a head takes its
; relation, so that the counts below read the space's rows through its trie.
(= (pad $n)
   (if (> $n 0)
       (let* (($m (- $n 1)) ($_ (add-atom &self (pad-a $n)))
              ($_ (add-atom &self (pad-b $n))))
             (pad $m))
       done))
!(pad 2100)
; A row holding variables meets each pattern it unifies with, once.
!(add-atom &self (edge $r $s))
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (edge $x $y) 2) 1)
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (, (edge $x $y) (edge $y $z)) 1) 0)
!(size-atom (collapse (match &self (edge $x $y) t)))
; A row holding variables beside a head's relation, alone and in a
; conjunction: the cursors count it with the relation's facts.
!(add-atom &self (link $p $q))
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (link $x $y) 1) 0)
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (, (link $x $y) (edge $y $z)) 1) 0)
!(foldall (|-> ($i $acc) (+ $acc $i)) (match &self (, (link $x $y) (link $y $z)) 1) 0)
