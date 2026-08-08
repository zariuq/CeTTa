(= (ofc $depth $hypcnt $tgt (: $x $a))
   (if (< 0 $hypcnt)
       (if (and (< 0 $depth) (<= $hypcnt $depth))
           (ofc-rec (- $depth 1) (- $hypcnt 1) $tgt (: $x $a))
           (empty))
       (let (: $x $a) $tgt (: $x $a))))
(= (ofc-rec $depth $hypcnt $tgt
            (: $f (-> (imp $p (imp $q $p)) $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax1) $b)))
(= (ofc-rec $depth $hypcnt $tgt
            (: $f (-> (imp (imp $p (imp $q $r)) (imp (imp $p $q) (imp $p $r))) $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax2) $b)))
(= (ofc-rec $depth $hypcnt $tgt
            (: $f (-> (imp (imp (neg $p) (neg $q)) (imp $q $p)) $b)))
   (ofc $depth $hypcnt $tgt (: ($f ax3) $b)))
(= (ofc-rec $depth $hypcnt $tgt (: $f (-> $b $c)))
   (ofc $depth (+ $hypcnt 2) $tgt (: (mpi $f) (-> (imp $a $b) (-> $a $c)))))
(= (obfc $depth (: $x $a))
   (ofc $depth 1 (: $x $a) (: I (-> $a $a))))

!(test (collapse (obfc 13 (: $x (imp (imp (imp p q) r) (imp q r)))))
       ((: ((((mpi (((mpi (mpi ((mpi ((mpi (mpi I)) ax2)) ax1))) ax2) ax2)) ax1) ax1) ax1) (imp (imp (imp p q) r) (imp q r)))
        (: (((mpi ((((mpi ((mpi (mpi ((mpi (mpi I)) ax2))) ax2)) ax1) ax2) ax1)) ax1) ax1) (imp (imp (imp p q) r) (imp q r)))))
