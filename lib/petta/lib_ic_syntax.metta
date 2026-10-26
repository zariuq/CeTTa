; Exact factoring of finite ordered syntax, matching SyntaxFactor.lean.
; IC:Leaf payloads are symbols, so equality is structural, not numeric promotion.
; Fresh IC:Ref names are representation references, never host bindings.
!(import! &self "lib/petta/lib_incremental_compression.metta")

(= (ic-tree:size $tree)
   (case $tree
     (((IC:Leaf $label) (if (== (get-metatype $label) Symbol) (IC:Some 1) IC:None))
      ((IC:Ref $name)
       (case (ic:nat-value $name)
         (((IC:Some $n) (IC:Some (+ $n 1))) ($other IC:None))))
      ((IC:Pair $left $right)
       (let* (($left-size (ic-tree:size $left)) ($right-size (ic-tree:size $right)))
         (case ($left-size $right-size)
           ((((IC:Some $l) (IC:Some $r)) (IC:Some (+ 1 (+ $l $r))))
            ($other IC:None)))))
      ($other IC:None))))

(= (ic-tree:occurs $name $tree)
   (case $tree
     (((IC:Leaf $label) False)
      ((IC:Ref $other) (== $name $other))
      ((IC:Pair $left $right) (or (ic-tree:occurs $name $left) (ic-tree:occurs $name $right))))))

(= (ic-tree:abstract $name $body $tree)
   (if (== $tree $body) (IC:Ref $name)
     (case $tree
       (((IC:Pair $left $right)
         (let* (($l (ic-tree:abstract $name $body $left)) ($r (ic-tree:abstract $name $body $right)))
           (IC:Pair $l $r)))
        ($other $other)))))

(= (ic-tree:expand $name $body $tree)
   (case $tree
     (((IC:Ref $other) (if (== $name $other) $body $tree))
      ((IC:Pair $left $right)
       (let* (($l (ic-tree:expand $name $body $left)) ($r (ic-tree:expand $name $body $right)))
         (IC:Pair $l $r)))
      ($other $other))))

(= (ic:extract IC:Syntax $feature $source)
   (case $feature
     (((IC:Factor $name $body)
       (let* (($valid-source (ic-tree:size $source)) ($valid-body (ic-tree:size $body))
              ($valid-name (ic:nat-value $name)))
         (case ($valid-source $valid-body $valid-name)
           ((((IC:Some $s) (IC:Some $b) (IC:Some $n))
             (if (ic-tree:occurs $name $source) IC:None
               (let $residual (ic-tree:abstract $name $body $source) (IC:Some $residual))))
            ($other IC:None)))))
      ($other IC:None))))

(= (ic:expand IC:Syntax $feature $residual)
   (case $feature
     (((IC:Factor $name $body)
       (let* (($valid-residual (ic-tree:size $residual)) ($valid-body (ic-tree:size $body))
              ($valid-name (ic:nat-value $name)))
         (case ($valid-residual $valid-body $valid-name)
           ((((IC:Some $r) (IC:Some $b) (IC:Some $n))
             (let $source (ic-tree:expand $name $body $residual) (IC:Some $source)))
            ($other IC:None)))))
      ($other IC:None))))

(= (ic:feature-cost IC:Syntax $feature)
   (case $feature
     (((IC:Factor $name $body)
       (let* (($valid-body (ic-tree:size $body)) ($valid-name (ic:nat-value $name)))
         (case ($valid-body $valid-name)
           ((((IC:Some $b) (IC:Some $n)) (IC:Some (+ $b (+ $n 2))))
            ($other IC:None)))))
      ($other IC:None))))

(= (ic:data-cost IC:Syntax $source) (ic-tree:size $source))

; These internal walkers receive trees admitted by ic:data-cost.
(= (ic-tree:max-reference $tree)
   (case $tree
     (((IC:Leaf $label) 0)
      ((IC:Ref $name) (case (ic:nat-value $name) (((IC:Some $n) $n))))
      ((IC:Pair $left $right)
       (let* (($l (ic-tree:max-reference $left)) ($r (ic-tree:max-reference $right)))
         (if (> $l $r) $l $r))))))

(= (ic-tree:subtrees $tree)
   (case $tree
     (((IC:Pair $left $right)
       (let* (($l (ic-tree:subtrees $left)) ($r (ic-tree:subtrees $right))
              ($tail (union-atom $l $r)))
         (cons-atom $tree $tail)))
      ($other (cons-atom $tree ())))))

(= (ic-tree:features $name $trees)
   (if (== $trees ()) ()
     (let ($body $rest) (decons-atom $trees)
       (let $tail (ic-tree:features $name $rest)
         (cons-atom (IC:Factor $name $body) $tail)))))

(= (ic:propose IC:Syntax $source)
   (let* (($last (ic-tree:max-reference $source))
          ($fresh (ic-internal:nat (+ $last 1)))
          ($trees (ic-tree:subtrees $source)))
     (ic-tree:features $fresh $trees)))
