; Depth-3 propositional forward chaining in the exact-atom nodup lane, with a
; final count result only.

(= (depth) 3)

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(= (fs2 $rb (: $x1 $a1) (: $x2 $a2))
   (match $rb (: $f (-> $a1 $a2 $b)) (: ($f $x1 $x2) $b)))

(= (fs $rb $ss)
   (fs2 $rb
        (match $ss (: $x1 $a1) (: $x1 $a1))
        (match $ss (: $x2 $a2) (: $x2 $a2))))

(= (fs-add-to-ss-nodup $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: $x1 $a1)
       (match $ss0 (: $x2 $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (add-atom-nodup $ss (: ($f $x1 $x2) $b)))))))

!(bind! &rb (new-space))
!(add-atom &rb (: ax-mp (-> (→ $p $q) $p $q)))

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $p (→ $q $p))))
!(add-atom &ab (: ax-2 (→ (→ $p (→ $q $r)) (→ (→ $p $q) (→ $p $r)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $p) (¬ $q)) (→ $q $p))))

!(bind! &ss (new-space))
!(match &ab (: $x $a) (add-atom-nodup &ss (: $x $a)))

!(if (<= 1 (depth)) (fs-add-to-ss-nodup &rb &ss) (empty))
!(if (<= 2 (depth)) (fs-add-to-ss-nodup &rb &ss) (empty))
!(if (<= 3 (depth)) (fs-add-to-ss-nodup &rb &ss) (empty))

!(count-atoms &ss)
