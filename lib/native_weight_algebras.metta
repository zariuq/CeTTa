; Coefficients interpreted by native weight:where and weight:read.
; An algebra descriptor supplies operations; it is not evidence of their laws.
; Numeric clients select exact rational arithmetic before evaluation.

(= (weights:boolean) (algebra False True or and))
(= (weights:natural) (algebra 0 1 + *))
(= (weights:rational) (algebra 0 1 + *))
(= (weights:complex)
   (algebra (Complex 0 0) (Complex 1 0) complex:add complex:mul))
; Import lib_wmpln_evidence.metta when using this descriptor. hplus denotes
; addition of evidence counts; a revision claim additionally needs disjoint
; evidence provenance (or the separate overlap-corrected revision service).
(= (weights:pln)
   (algebra (evidence 0 0) (evidence 1 1) evidence-hplus evidence-tensor))

; Construct a list from computed values, respecting Prime's syntax-level cons.
(= (weights:cons $head $tail)
   (let* (($h $head) ($t $tail)) (cons-atom $h $t)))

(= (weights:length $items)
   (if (== $items ()) 0
     (let ($head $tail) (decons-atom $items) (+ 1 (weights:length $tail)))))
(= (weights:natural? $n)
   (if (< $n 0) False
     (if (== $n 0) True (weights:natural? (- $n 1)))))
(= (weights:repeat $value $n)
   (if (== $n 0) () (weights:cons $value (weights:repeat $value (- $n 1)))))
(= (weights:at $items $index)
   (let ($head $tail) (decons-atom $items)
     (if (== $index 0) $head (weights:at $tail (- $index 1)))))

(= (vector:zip $op $left $right)
   (if (== $left ())
     (if (== $right ()) () (empty))
     (if (== $right ()) (empty)
       (let* ((($a $as) (decons-atom $left))
              (($b $bs) (decons-atom $right)))
         (weights:cons ($op $a $b) (vector:zip $op $as $bs))))))
(= (vector:add (Vector $a) (Vector $b)) (Vector (vector:zip + $a $b)))
(= (vector:mul (Vector $a) (Vector $b)) (Vector (vector:zip * $a $b)))
(= (weights:vector $n)
   (if (weights:natural? $n)
     (algebra (Vector (weights:repeat 0 $n)) (Vector (weights:repeat 1 $n))
              vector:add vector:mul)
     (WeightShapeMismatch (vector $n))))
(= (vector:le-list $left $right)
   (if (== $left ()) (== $right ())
     (if (== $right ()) False
       (let* ((($a $as) (decons-atom $left))
              (($b $bs) (decons-atom $right)))
         (and (<= $a $b) (vector:le-list $as $bs))))))
(= (vector:dominates? (Vector $a) (Vector $b))
   (and (vector:le-list $a $b) (not (== $a $b))))

; Dense row-major matrices. Rectangular multiplication is indexed contraction;
; only the square n-by-n family below has a semiring descriptor.
(= (matrix:valid? (Matrix $m $n $entries))
   (and (weights:natural? $m)
     (and (weights:natural? $n) (== (weights:length $entries) (* $m $n)))))
(= (matrix:add (Matrix $m $n $a) (Matrix $p $q $b))
   (if (and (and (== $m $p) (== $n $q))
            (and (matrix:valid? (Matrix $m $n $a))
                 (matrix:valid? (Matrix $p $q $b))))
     (Matrix $m $n (vector:zip + $a $b)) (empty)))
(= (matrix:dot $a $b $n $k $i $j $t)
   (if (== $t $n) 0
     (+ (* (weights:at $a (+ (* $i $n) $t))
           (weights:at $b (+ (* $t $k) $j)))
        (matrix:dot $a $b $n $k $i $j (+ $t 1)))))
(= (matrix:entries $a $b $m $n $k $i $j)
   (if (or (== $i $m) (== $k 0)) ()
     (weights:cons (matrix:dot $a $b $n $k $i $j 0)
       (if (== (+ $j 1) $k)
         (matrix:entries $a $b $m $n $k (+ $i 1) 0)
         (matrix:entries $a $b $m $n $k $i (+ $j 1))))))
(= (matrix:mul (Matrix $m $n $a) (Matrix $p $k $b))
   (if (and (== $n $p)
            (and (matrix:valid? (Matrix $m $n $a))
                 (matrix:valid? (Matrix $p $k $b))))
     (Matrix $m $k (matrix:entries $a $b $m $n $k 0 0)) (empty)))
(= (matrix:identity-entries $n $i $j)
   (if (== $i $n) ()
     (weights:cons (if (== $i $j) 1 0)
       (if (== (+ $j 1) $n) (matrix:identity-entries $n (+ $i 1) 0)
         (matrix:identity-entries $n $i (+ $j 1))))))
(= (weights:matrix $n)
   (if (weights:natural? $n)
     (algebra (Matrix $n $n (weights:repeat 0 (* $n $n)))
              (Matrix $n $n (matrix:identity-entries $n 0 0)) matrix:add matrix:mul)
     (WeightShapeMismatch (square-matrix $n))))

(= (complex:add (Complex $a $b) (Complex $c $d)) (Complex (+ $a $c) (+ $b $d)))
(= (complex:mul (Complex $a $b) (Complex $c $d))
   (Complex (- (* $a $c) (* $b $d)) (+ (* $a $d) (* $b $c))))
(= (complex:born (Complex $a $b)) (+ (* $a $a) (* $b $b)))

; Finite declared observations. Contribution provenance remains available
; after summation; no support or normalization is implicit in weight:read.
(= (weights:coefficient (algebra $zero $one $plus $times) $value $items)
   (if (== $items ()) $zero
     (let ((Weighted $v $coefficient $provenance) $tail) (decons-atom $items)
       (if (== $v $value)
         ($plus $coefficient
           (weights:coefficient (algebra $zero $one $plus $times) $value $tail))
         (weights:coefficient (algebra $zero $one $plus $times) $value $tail)))))
(= (weights:matching $value $items)
   (if (== $items ()) ()
     (let ($head $tail) (decons-atom $items)
       (let (Weighted $v $coefficient $provenance) $head
         (if (== $v $value) (weights:cons $head (weights:matching $value $tail))
           (weights:matching $value $tail))))))
(= (weights:others $value $items)
   (if (== $items ()) ()
     (let ($head $tail) (decons-atom $items)
       (let (Weighted $v $coefficient $provenance) $head
         (if (== $v $value) (weights:others $value $tail)
           (weights:cons $head (weights:others $value $tail)))))))
(= (weights:aggregate $algebra $items)
   (if (== $items ()) ()
     (let ((Weighted $value $coefficient $provenance) $tail) (decons-atom $items)
       (weights:cons
         (WeightedSum $value (weights:coefficient $algebra $value $items)
           (contributions (weights:matching $value $items)))
         (weights:aggregate $algebra (weights:others $value $tail))))))
(= (weights:support $zero $sums)
   (if (== $sums ()) ()
     (let ($head $tail) (decons-atom $sums)
       (let (WeightedSum $value $coefficient $provenance) $head
         (if (== $coefficient $zero) (weights:support $zero $tail)
           (weights:cons $head (weights:support $zero $tail)))))))
(= (weights:born-total $sums)
   (if (== $sums ()) 0
     (let ((WeightedSum $value $coefficient $provenance) $tail) (decons-atom $sums)
       (+ (complex:born $coefficient) (weights:born-total $tail)))))
(= (weights:born-normalize $total $sums)
   (if (== $sums ()) ()
     (let ((WeightedSum $value $coefficient $provenance) $tail) (decons-atom $sums)
       (weights:cons (Probability $value (/ (complex:born $coefficient) $total) $provenance)
                  (weights:born-normalize $total $tail)))))
(= (weights:born $sums)
   (let $total (weights:born-total $sums)
     (if (== $total 0) (BornUndefined zero-norm)
       (weights:born-normalize $total $sums))))

(= (weights:dominated? $coefficient $items)
   (if (== $items ()) False
     (let ((Weighted $value $candidate $provenance) $tail) (decons-atom $items)
       (or (vector:dominates? $candidate $coefficient)
           (weights:dominated? $coefficient $tail)))))
(= (weights:pareto-loop $remaining $all)
   (if (== $remaining ()) ()
     (let ($head $tail) (decons-atom $remaining)
       (let (Weighted $value $coefficient $provenance) $head
         (if (weights:dominated? $coefficient $all)
           (weights:pareto-loop $tail $all)
           (weights:cons $head (weights:pareto-loop $tail $all)))))))
(= (weights:pareto $items) (weights:pareto-loop $items $items))

(= (weights:values $items)
   (if (== $items ()) ()
     (let ((Weighted $value $coefficient $provenance) $tail) (decons-atom $items)
       (weights:cons $value (weights:values $tail)))))

; Exact tropical coefficients over rational costs with an explicit top value.
; TropicalInfinity is additive identity and multiplicatively absorbing.
; TropicalFinite admits every exact rational, including negative costs.
; The descriptor does not grant the numeric nonnegative-sum pruning licence.

(= (weights:tropical)
   (algebra TropicalInfinity (TropicalFinite 0) tropical:add tropical:mul))
(= (tropical:valid? $value)
   (if (== $value TropicalInfinity) True
     (if (== (get-metatype $value) Expression)
       (if (== (size-atom $value) 2)
         (and (== (car-atom $value) TropicalFinite)
              (weight:exact-rational? (weights:at $value 1)))
         False)
       False)))
(= (tropical:add $left $right)
   (if (and (tropical:valid? $left) (tropical:valid? $right))
     (if (== $left TropicalInfinity) $right
       (if (== $right TropicalInfinity) $left
         (let* (((TropicalFinite $x) $left) ((TropicalFinite $y) $right))
           (TropicalFinite (if (< $x $y) $x $y)))))
     (TropicalInvalidOperands $left $right)))
(= (tropical:mul $left $right)
   (if (and (tropical:valid? $left) (tropical:valid? $right))
     (if (or (== $left TropicalInfinity) (== $right TropicalInfinity))
       TropicalInfinity
       (let* (((TropicalFinite $x) $left) ((TropicalFinite $y) $right))
         (TropicalFinite (+ $x $y))))
     (TropicalInvalidOperands $left $right)))
(= (tropical:compare $left $right)
   (if (and (tropical:valid? $left) (tropical:valid? $right))
     (if (== $left $right) Equal
       (if (== $left TropicalInfinity) Greater
         (if (== $right TropicalInfinity) Less
           (let* (((TropicalFinite $x) $left) ((TropicalFinite $y) $right))
             (if (< $x $y) Less Greater)))))
     (TropicalInvalidOperands $left $right)))
