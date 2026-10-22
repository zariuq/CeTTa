(: tree-weight (-> Atom %Undefined%))
(= (tree-weight $tree)
   (case $tree
     (((Tip $ignored) 1)
      ((Branch $label $children)
       (let $inside (forest-weight $children) (+ 1 $inside)))
      ((Fork $left $right)
       (let* (($a (tree-weight $left)) ($b (tree-weight $right)))
         (+ 1 (+ $a $b)))))))
(: forest-weight (-> Atom %Undefined%))
(= (forest-weight $trees)
   (case $trees
     ((WeightNil 0)
      ((WeightCons $head $tail)
       (let* (($a (tree-weight $head)) ($b (forest-weight $tail))) (+ $a $b))))))

; Every declared result guard remains part of call admission.
(: guarded-weight (-> Atom String))
(= (guarded-weight $tree)
   (case $tree (((Tip $ignored) 1) ((Fork $a $b)
     (let* (($x (guarded-weight $a)) ($y (guarded-weight $b))) (+ $x $y))))))

; An unused binding still computes, and can fail.
(: unused-weight (-> Atom %Undefined%))
(= (unused-weight $tree)
   (case $tree (((Tip $ignored) 1) ((Fork $a $b)
     (let $unused (tree-weight $a) 9)))))

; Reusing one scalar binding is outside the linear normalization.
(: twice-weight (-> Atom %Undefined%))
(= (twice-weight $tree)
   (case $tree (((Tip $ignored) 1) ((Fork $a $b)
     (let $weight (tree-weight $a) (+ $weight $weight))))))

(: huge-weight (-> Atom %Undefined%))
(= (huge-weight $tree)
   (case $tree ((HugeEnd 9223372036854775807)
     ((HugeMore $child) (+ 1 (huge-weight $child))))))

(= (weight-effect) (progn (add-atom &self (weight-touched)) touched))
!(tree-weight (Tip ignored))
!(tree-weight (Branch label (WeightCons (Tip a) (WeightCons (Fork (Tip b) (Tip c)) WeightNil))))
!(let $shared (Tip payload) (tree-weight (Fork $shared $shared)))
!(tree-weight (Tip (weight-effect)))
!(collapse (match &self (weight-touched) touched))
!(collapse (let $meta (superpose (first second)) (tree-weight (Tip $meta))))
!(collapse (tree-weight (Fork Invalid (Tip b))))
!(collapse (guarded-weight (Tip ignored)))
!(collapse (unused-weight (Fork Invalid (Tip b))))
!(twice-weight (Fork (Fork (Tip a) (Tip b)) (Tip c)))
!(huge-weight (HugeMore HugeEnd))
!(tree-weight (Tip $unbound))
!(once (tree-weight $open))
!(collapse (let 1 (tree-weight (Fork (Tip a) (Tip b))) done))

; A cached fold depends on every called helper, not only its entry equation.
(: revision-leaf (-> Atom %Undefined%))
(= (revision-leaf $tree) (case $tree (((RevisionTip $payload) 1))))
(: revision-root (-> Atom %Undefined%))
(= (revision-root $tree)
   (case $tree (((RevisionBox $child) (revision-leaf $child)))))
!(revision-root (RevisionBox (RevisionTip a)))
!(progn
   (add-atom &self (= (revision-leaf $tree)
     (case $tree (((RevisionTip $payload) 2)))))
   (collapse (revision-root (RevisionBox (RevisionTip a)))))
