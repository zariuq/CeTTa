; Scaling probe for 01_nested_spaces: three levels of spaces below an index,
; ten at each level, 10 + 100 + 1,000 = 1,110 spaces.  The index names ten
; spaces by (child I SPACE); each of those names ten, and each of those names
; ten leaves.  Leaf (I J K) holds (val 100I+10J+K).
;
; Phases, each answering the sum of the values found and the firings taken:
;   one path query per leaf (1,000), each as one firing of four staged sources;
;   the same paths as a chain of four firings;
;   the same paths as nested match (no firing);
;   the sweep over every leaf: one firing with `every`; nested match;
;   a modification in every leaf, one firing per leaf, and then in every leaf
;   at once in one firing.
;
; With no argument the program runs every phase and prints only answers, as
; checked in the .expected file.  An argument selects phases for measurement:
;   build | paths-atomic | paths-chain | paths-match | sweep | modify
;   timed     every phase, each with its milliseconds (varies run to run)
!(import! &self interaction)
!(import! &self system)

(= (mode) (if (system:has-args) (system:arg 0) all))
(= (on $p) (let $m (mode) (if (== $m $p) True (if (== $m all) True (== $m timed)))))
(= (fires) (let (agenda $parked $ready $parks $attempts (fires $n) $woken $unchanged) (agenda-stats) $n))
(= (sum-of $xs) (foldl-atom $xs 0 $acc $x (+ $acc $x)))

(: run-phase (-> Atom Atom %Undefined%))
(= (run-phase $name $q)
   (let* (($f0 (fires)) ($t0 (system:monotonic-ns)) ($r (eval $q)) ($t1 (system:monotonic-ns)) ($f1 (fires)))
     (if (== (mode) timed)
         ($name $r (steps (- $f1 $f0)) (ms (/ (- $t1 $t0) 1000000)))
         ($name $r (steps (- $f1 $f0))))))

; ---- the graph
!(bind! &index (new-space))
(= (build-leaves $mid $i $j $k)
   (if (> $k 9) done
       (let* (($leaf (new-space))
              ($v (+ (* 100 $i) (+ (* 10 $j) $k)))
              ($_1 (add-atom $leaf (val $v)))
              ($_2 (add-atom $mid (child $k $leaf))))
         (build-leaves $mid $i $j (+ $k 1)))))
(= (build-mids $top $i $j)
   (if (> $j 9) done
       (let* (($mid (new-space))
              ($_1 (build-leaves $mid $i $j 0))
              ($_2 (add-atom $top (child $j $mid))))
         (build-mids $top $i (+ $j 1)))))
(= (build-tops $i)
   (if (> $i 9) (spaces 1110)
       (let* (($top (new-space))
              ($_1 (build-mids $top $i 0))
              ($_2 (add-atom &index (child $i $top))))
         (build-tops (+ $i 1)))))

; ---- the three forms of a path query
(= (path-atomic $i $j $k)
   (transform &index
     (, (read (child $i $a)) (read $a (child $j $b)) (read $b (child $k $c)) (read $c (val $v)))
     $v){$i $j $k})
(= (path-chain $i $j $k)
   (let* (($a (transform &index (read (child $i $a)) $a){$i})
          ($b (transform $a (read (child $j $b)) $b){$j})
          ($c (transform $b (read (child $k $c)) $c){$k}))
     (transform $c (read (val $v)) $v)))
(= (path-match $i $j $k)
   (match &index (child $i $a)
     (match $a (child $j $b)
       (match $b (child $k $c)
         (match $c (val $v) $v)){$k}){$j}){$i})

; every leaf (I J K), I, J, K from 0 to 9
(= (each-leaf $f $i $j $k $acc)
   (if (> $i 9) $acc
       (if (> $j 9) (each-leaf $f (+ $i 1) 0 0 $acc)
           (if (> $k 9) (each-leaf $f $i (+ $j 1) 0 $acc)
               (let $v ($f $i $j $k) (each-leaf $f $i $j (+ $k 1) (+ $acc $v)))))))

(= (sweep-atomic)
   (sum-of (collapse (transform &index
     (every (, (read (child $i $a)) (read $a (child $j $b)) (read $b (child $k $c)) (read $c (val $v))))
     $v))))
(= (sweep-match)
   (sum-of (collapse (match &index (child $i $a) (match $a (child $j $b) (match $b (child $k $c) (match $c (val $v) $v)))))))

; ---- a modification three spaces down: the leaf's value goes up by one
(= (bump $i $j $k)
   (transform &index
     (, (read (child $i $a)) (read $a (child $j $b)) (read $b (child $k $c)) (take $c (val $v)) (let $w (+ $v 1)))
     (, (add-atom $c (val $w)) 1)){$i $j $k})
(= (bump-all)
   (sum-of (collapse (transform &index
     (every (, (read (child $i $a)) (read $a (child $j $b)) (read $b (child $k $c)) (take $c (val $v)) (let $w (+ $v 1))))
     (, (add-atom $c (val $w)) 1)))))

!(run-phase build (build-tops 0))
!(if (on paths-atomic) (run-phase paths-one-firing (each-leaf path-atomic 0 0 0 0)) skipped)
!(if (on paths-chain) (run-phase paths-four-firings (each-leaf path-chain 0 0 0 0)) skipped)
!(if (on paths-match) (run-phase paths-nested-match (each-leaf path-match 0 0 0 0)) skipped)
!(if (on sweep) (run-phase sweep-one-firing (sweep-atomic)) skipped)
!(if (on sweep) (run-phase sweep-nested-match (sweep-match)) skipped)
!(if (on modify) (run-phase modify-one-firing-per-leaf (each-leaf bump 0 0 0 0)) skipped)
!(if (on modify) (run-phase modify-all-in-one-firing (bump-all)) skipped)
!(if (on modify) (run-phase sum-after-modify (sweep-match)) skipped)
