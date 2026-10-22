; Call-by-push-value over the contextual effect signature.
;
; This library realizes the ContextualCBPV presentation
; (Mettapedia/metta/cbpv/ContextualCBPV.module) under --lang prime.  Terms use
; the presentation's constructors; a bound variable is (Var n), counted from
; the nearest binder, as in the Lean syntax (TypeTheory/ContextualCBPVSyntax).
;
;   values        (Const d) (Prim op v) Unit (Pair v w) (Thunk m)
;   computations  (Return v) (Bind m n) (Force v) (Lam n) (App m v)
;                 (Split v n) (Choose m m') (Read n) (Write v m) (Intent v m)
;                 (Raise kind v) (Handle m kind onReturn onRaise)
;   raise kinds   ErrorKind InertKind
;   zero answers  Zero, the nullary choice: no world
;   need          (LetNeed m n): a memo cell for m, bound as (Var 0) in n
;   MeTTa data    values (Make head (v ...)) (List (v ...)) (Delay src m),
;                 computations (CaseMake v head arity n) (CaseTuple v len n m')
;                 (IfEq v w n m') (Drop k m) (Demand v) (Observe v)
;                 (MapList v n) (FoldList v w n), with the binary primitives
;                 (Prim2 op v w)
;
; In (Bind m n), (Lam n), (Read n), (LetNeed m n) and the two handler
; clauses, n binds one variable; (Split v n) binds the second component as
; (Var 0) and the first as (Var 1).
;
; (cbpv:run term state) is the isolated-world observer of the Lean semantics
; (runWorlds of the denotation).  It answers one
;     (World branch outcome state intents)
; per world: the branch path with the most recent choice first, the outcome
; (Ok value) or (Raised kind payload), the world's final state, and its
; deferred intents in the order they were requested.  A choice gives two
; worlds that both start from the parent's state and memo cells; a raise
; propagates through Bind and App until a Handle for its kind catches it.
;
; Forcing an ordinary thunk runs its body again.  Forcing a memo cell runs
; its producer at the first demand on the current path and serves that value
; to every later demand there (TypeTheory/ContextualCBPVNeed, cell_none).
; Each branch of a choice keeps its own copy of the cells.
;
; The interpreter is an environment machine: a thunk, a function or a memo
; cell closes over its environment instead of being substituted into.  Its
; observations are compared with the Lean semantics on tests/prime/cbpv/;
; that agreement is tested, not proved.  Recursion is not part of this
; calculus.

; ---- environments and the memo heap
(= (cbpv:lookup (Cons $v $rest) $n)
   (if (== $n 0) $v (cbpv:lookup $rest (- $n 1))))

(= (cbpv:size Nil) 0)
(= (cbpv:size (Cons $x $rest)) (let $n (cbpv:size $rest) (+ $n 1)))

(= (cbpv:cell (Cons (Cell $id $status) $rest) $want)
   (if (== $id $want) $status (cbpv:cell $rest $want)))

(= (cbpv:settle Nil $want $value) Nil)
(= (cbpv:settle (Cons (Cell $id $status) $rest) $want $value)
   (if (== $id $want)
       (Cons (Cell $id (Done $value)) $rest)
       (let $more (cbpv:settle $rest $want $value) (Cons (Cell $id $status) $more))))

; ---- pure primitives
(= (cbpv:prim succ $d) (+ $d 1))

; ---- values
(= (cbpv:value $env (Var $n)) (cbpv:lookup $env $n))
(= (cbpv:value $env (Const $d)) (VConst $d))
(= (cbpv:value $env (Prim $op $v))
   (let (VConst $d) (cbpv:value $env $v)
     (let $r (cbpv:prim $op $d) (VConst $r))))
(= (cbpv:value $env Unit) VUnit)
(= (cbpv:value $env (Pair $a $b))
   (let $x (cbpv:value $env $a) (let $y (cbpv:value $env $b) (VPair $x $y))))
(= (cbpv:value $env (Thunk $m)) (VClo $env $m))
; MeTTa data: a constructor applied to values, and binary grounded primitives.
(= (cbpv:value $env (Make $head $args))
   (let $vals (cbpv:values $env $args) (VMake $head $vals)))
(= (cbpv:value $env (Prim2 $op $a $b))
   (let (VConst $x) (cbpv:value $env $a)
     (let (VConst $y) (cbpv:value $env $b)
       (let $r (cbpv:prim2 $op $x $y) (VConst $r)))))
(= (cbpv:values $env $args)
   (if (== $args ()) ()
       (let $first (car-atom $args)
         (let $rest (cdr-atom $args)
           (let $v (cbpv:value $env $first)
             (let $vs (cbpv:values $env $rest) (cons-atom $v $vs)))))))
(= (cbpv:prim2 + $x $y) (+ $x $y))
(= (cbpv:prim2 - $x $y) (- $x $y))
(= (cbpv:prim2 * $x $y) (* $x $y))

; ---- computations: (W terminal state branch reversed-intents heap) per world
(= (cbpv:comp $env (Return $v) $s $b $i $h)
   (let $x (cbpv:value $env $v) (W (Ret $x) $s $b $i $h)))

(= (cbpv:comp $env (Bind $m $n) $s $b $i $h)
   (let (W $t $s1 $b1 $i1 $h1) (cbpv:comp $env $m $s $b $i $h)
     (cbpv:then $env $t $n $s1 $b1 $i1 $h1)))
(= (cbpv:then $env (Ret $x) $n $s $b $i $h) (cbpv:comp (Cons $x $env) $n $s $b $i $h))
(= (cbpv:then $env (Raised $k $p) $n $s $b $i $h) (W (Raised $k $p) $s $b $i $h))

(= (cbpv:comp $env (Force $v) $s $b $i $h)
   (let $x (cbpv:value $env $v) (cbpv:force $x $s $b $i $h)))
(= (cbpv:force (VClo $e $m) $s $b $i $h) (cbpv:comp $e $m $s $b $i $h))
(= (cbpv:force (VNeed $id) $s $b $i $h)
   (let $status (cbpv:cell $h $id) (cbpv:demand $id $status $s $b $i $h)))
(= (cbpv:demand $id (Done $v) $s $b $i $h) (W (Ret $v) $s $b $i $h))
(= (cbpv:demand $id (Pending $e $m) $s $b $i $h)
   (let (W $t $s1 $b1 $i1 $h1) (cbpv:comp $e $m $s $b $i $h)
     (cbpv:commit $id $t $s1 $b1 $i1 $h1)))
(= (cbpv:commit $id (Ret $v) $s $b $i $h)
   (let $h2 (cbpv:settle $h $id $v) (W (Ret $v) $s $b $i $h2)))
(= (cbpv:commit $id (Raised $k $p) $s $b $i $h) (W (Raised $k $p) $s $b $i $h))

(= (cbpv:comp $env (LetNeed $m $n) $s $b $i $h)
   (let $id (cbpv:size $h)
     (cbpv:comp (Cons (VNeed $id) $env) $n $s $b $i (Cons (Cell $id (Pending $env $m)) $h))))

(= (cbpv:comp $env (Lam $n) $s $b $i $h) (W (LamClo $env $n) $s $b $i $h))

(= (cbpv:comp $env (App $m $v) $s $b $i $h)
   (let (W $t $s1 $b1 $i1 $h1) (cbpv:comp $env $m $s $b $i $h)
     (let $a (cbpv:value $env $v) (cbpv:apply $t $a $s1 $b1 $i1 $h1))))
(= (cbpv:apply (LamClo $e $n) $a $s $b $i $h) (cbpv:comp (Cons $a $e) $n $s $b $i $h))
(= (cbpv:apply (Raised $k $p) $a $s $b $i $h) (W (Raised $k $p) $s $b $i $h))

(= (cbpv:comp $env (Split $v $n) $s $b $i $h)
   (let (VPair $x $y) (cbpv:value $env $v)
     (cbpv:comp (Cons $y (Cons $x $env)) $n $s $b $i $h)))

; Case on MeTTa data: a value built by `head` with `arity` fields binds them,
; the last field as (Var 0); any other value has no world.
(= (cbpv:comp $env (CaseMake $v $head $arity $n) $s $b $i $h)
   (let $x (cbpv:value $env $v) (cbpv:case-make $env $x $head $arity $n $s $b $i $h)))
(= (cbpv:case-make $env (VMake $actual $fields) $head $arity $n $s $b $i $h)
   (let $size (size-atom $fields)
   (if (and (== $actual $head) (== $size $arity))
       (let $env2 (cbpv:push-all $fields $env) (cbpv:comp $env2 $n $s $b $i $h))
       (empty))))
(= (cbpv:case-make $env (VConst $d) $head $arity $n $s $b $i $h)
   (let $meta (get-metatype $d)
     (if (== $meta Expression)
         (cbpv:case-datum $env (VConst $d) $head $arity $n $s $b $i $h)
         (if (and (== $d $head) (== $arity 0)) (cbpv:comp $env $n $s $b $i $h) (empty)))))
; A datum (h d1 ... dn) is the constructor h applied to the data d1 ... dn.
(= (cbpv:case-datum $env $x $head $arity $n $s $b $i $h)
   (let (VConst $d) $x
     (if (== $d ()) (empty)
         (let $actual (car-atom $d)
           (let $fields (cdr-atom $d)
             (let $size (size-atom $fields)
               (if (and (== $actual $head) (== $size $arity))
                   (let $values (cbpv:consts $fields)
                     (let $env2 (cbpv:push-all $values $env) (cbpv:comp $env2 $n $s $b $i $h)))
                   (empty))))))))
(= (cbpv:consts $fields)
   (if (== $fields ()) ()
       (let $first (car-atom $fields)
         (let $rest (cdr-atom $fields)
           (let $more (cbpv:consts $rest) (cons-atom (VConst $first) $more))))))
(= (cbpv:push-all $fields $env)
   (if (== $fields ()) $env
       (let $first (car-atom $fields)
         (let $rest (cdr-atom $fields) (cbpv:push-all $rest (Cons $first $env))))))

(= (cbpv:comp $env (Choose $l $r) $s $b $i $h)
   (superpose ((cbpv:comp $env $l $s (Cons False $b) $i $h)
               (cbpv:comp $env $r $s (Cons True $b) $i $h))))

(= (cbpv:comp $env (Read $n) $s $b $i $h)
   (cbpv:comp (Cons (VConst $s) $env) $n $s $b $i $h))

(= (cbpv:comp $env (Write $v $m) $s $b $i $h)
   (let (VConst $s2) (cbpv:value $env $v) (cbpv:comp $env $m $s2 $b $i $h)))

(= (cbpv:comp $env (Intent $v $m) $s $b $i $h)
   (let (VConst $q) (cbpv:value $env $v) (cbpv:comp $env $m $s $b (Cons $q $i) $h)))

; The nullary choice has no world; it passes through `to`, application and
; handlers, and beside an alternative it contributes nothing.  It is not a
; raise: no handler catches it (TypeTheory/ContextualCBPVZero).
(= (cbpv:comp $env Zero $s $b $i $h) (empty))

(= (cbpv:comp $env (Raise $k $v) $s $b $i $h)
   (let (VConst $p) (cbpv:value $env $v) (W (Raised $k $p) $s $b $i $h)))

(= (cbpv:comp $env (Handle $m $k $n $g) $s $b $i $h)
   (let (W $t $s1 $b1 $i1 $h1) (cbpv:comp $env $m $s $b $i $h)
     (cbpv:handled $env $t $k $n $g $s1 $b1 $i1 $h1)))
(= (cbpv:handled $env (Ret $x) $k $n $g $s $b $i $h)
   (cbpv:comp (Cons $x $env) $n $s $b $i $h))
(= (cbpv:handled $env (Raised $j $p) $k $n $g $s $b $i $h)
   (if (== $j $k)
       (cbpv:comp (Cons (VConst $p) $env) $g $s $b $i $h)
       (W (Raised $j $p) $s $b $i $h)))

; ---- the observer
(= (cbpv:readout (VConst $d)) $d)
(= (cbpv:readout VUnit) ())
(= (cbpv:readout (VPair $x $y))
   (let $a (cbpv:readout $x) (let $c (cbpv:readout $y) ($a $c))))
(= (cbpv:readout (VClo $e $m)) thunk)
(= (cbpv:readout (VNeed $id)) need-cell)
(= (cbpv:readout (VMake $head $fields))
   (let $parts (cbpv:readouts $fields) (cons-atom $head $parts)))
(= (cbpv:readouts $fields)
   (if (== $fields ()) ()
       (let $first (car-atom $fields)
         (let $rest (cdr-atom $fields)
           (let $r (cbpv:readout $first)
             (let $rs (cbpv:readouts $rest) (cons-atom $r $rs)))))))

(= (cbpv:outcome (Ret $x)) (let $r (cbpv:readout $x) (Ok $r)))
(= (cbpv:outcome (Raised $k $p)) (Raised $k $p))
(= (cbpv:outcome (LamClo $e $n)) function)

(= (cbpv:reverse Nil $acc) $acc)
(= (cbpv:reverse (Cons $x $rest) $acc) (cbpv:reverse $rest (Cons $x $acc)))

(= (cbpv:run $term $state)
   (let (W $t $s $b $i $h) (cbpv:comp Nil $term $state Nil Nil Nil)
     (let $o (cbpv:outcome $t)
       (let $is (cbpv:reverse $i Nil) (World $b $o $s $is)))))

; The outcomes alone.
(= (cbpv:outcomes $term $state)
   (let (World $b $o $s $is) (cbpv:run $term $state) $o))

;; ---- Matching, observation and suspensions of MeTTa data
;
; (CaseTuple v len n else): an expression value of `len` elements (a datum, a
; constructor application, whose head is its first element, or a list) binds
; its elements in n, the last as (Var 0); any other value runs else.
; (IfEq v w n else): n when the two values read out equal, else otherwise.
; (Drop k m): m in the environment without its k most recent entries.
; (Demand v): the value of a memo cell, forced at this demand; any other
; value as it is.
; (Observe v): the value with every memo cell inside it forced, as the answer
; boundary observes it; an explicit suspension stays as it is.
; (MapList v body), (FoldList v init body): body run once per element of the
; list value v, the element as (Var 0) (and the accumulator as (Var 1)).
; Values: (Delay src m), an explicit suspension of m written src, which
; forcing runs and which reads out as (delay src); (List (v1 ... vn)), the
; expression of the values.
(= (cbpv:value $env (Delay $src $m)) (VSusp $src $env $m))
(= (cbpv:value $env (List $args)) (let $vals (cbpv:values $env $args) (VList $vals)))
(= (cbpv:force (VSusp $src $e $m) $s $b $i $h) (cbpv:comp $e $m $s $b $i $h))
(= (cbpv:readout (VSusp $src $e $m)) (let $d delay (cons-atom $d ($src))))
(= (cbpv:readout (VList $vals)) (cbpv:readouts $vals))

(= (cbpv:tuple-parts (VConst $d))
   (let $meta (get-metatype $d)
     (if (== $meta Expression)
         (if (== $d ()) (parts ()) (let $vals (cbpv:consts $d) (parts $vals)))
         none)))
(= (cbpv:tuple-parts (VMake $head $fields)) (let $all (cons-atom (VConst $head) $fields) (parts $all)))
(= (cbpv:tuple-parts (VList $vals)) (parts $vals))
(= (cbpv:tuple-parts (VPair $x $y)) (parts ($x $y)))
(= (cbpv:tuple-parts VUnit) (parts ()))
(= (cbpv:tuple-parts (VClo $e $m)) none)
(= (cbpv:tuple-parts (VNeed $id)) none)
(= (cbpv:tuple-parts (VSusp $src $e $m)) none)

(= (cbpv:comp $env (CaseTuple $v $len $n $g) $s $b $i $h)
   (let $x (cbpv:value $env $v)
     (let $found (cbpv:tuple-parts $x)
       (if (== $found none) (cbpv:comp $env $g $s $b $i $h)
           (let (parts $elems) $found
             (let $size (size-atom $elems)
               (if (== $size $len)
                   (let $env2 (cbpv:push-all $elems $env) (cbpv:comp $env2 $n $s $b $i $h))
                   (cbpv:comp $env $g $s $b $i $h))))))))

(= (cbpv:comp $env (IfEq $v $w $n $g) $s $b $i $h)
   (let $x (cbpv:value $env $v)
     (let $y (cbpv:value $env $w)
       (let $rx (cbpv:readout $x)
         (let $ry (cbpv:readout $y)
           (if (== $rx $ry) (cbpv:comp $env $n $s $b $i $h) (cbpv:comp $env $g $s $b $i $h)))))))

(= (cbpv:comp $env (Drop $k $m) $s $b $i $h)
   (let $env2 (cbpv:drop $env $k) (cbpv:comp $env2 $m $s $b $i $h)))

(= (cbpv:comp $env (Demand $v) $s $b $i $h)
   (let $x (cbpv:value $env $v) (cbpv:demand-value $x $s $b $i $h)))
(= (cbpv:demand-value $x $s $b $i $h)
   (if (cbpv:is-cell $x) (cbpv:force $x $s $b $i $h) (W (Ret $x) $s $b $i $h)))
(= (cbpv:is-cell $x) (if (== $x VUnit) False (let $tag (car-atom $x) (== $tag VNeed))))
(= (cbpv:tag $x) (if (== $x VUnit) VUnit (car-atom $x)))

(= (cbpv:comp $env (Observe $v) $s $b $i $h)
   (let $x (cbpv:value $env $v) (cbpv:observe $x $s $b $i $h)))
(= (cbpv:observe $x $s $b $i $h)
   (let $tag (cbpv:tag $x) (cbpv:observe-tag $tag $x $s $b $i $h)))
(= (cbpv:observe-tag VNeed $x $s $b $i $h)
   (let (W $t $s1 $b1 $i1 $h1) (cbpv:force $x $s $b $i $h)
     (cbpv:observe-ret $t $s1 $b1 $i1 $h1)))
(= (cbpv:observe-tag VMake $x $s $b $i $h)
   (let (VMake $head $fields) $x
     (let (W $t $s1 $b1 $i1 $h1) (cbpv:observe-all $fields $s $b $i $h)
       (cbpv:observe-make $head $t $s1 $b1 $i1 $h1))))
(= (cbpv:observe-tag VList $x $s $b $i $h)
   (let (VList $vals) $x (cbpv:observe-all $vals $s $b $i $h)))
(= (cbpv:observe-tag VPair $x $s $b $i $h)
   (let (VPair $p $q) $x
     (let (W $t $s1 $b1 $i1 $h1) (cbpv:observe-all ($p $q) $s $b $i $h)
       (cbpv:observe-pair $t $s1 $b1 $i1 $h1))))
(= (cbpv:observe-tag VConst $x $s $b $i $h) (W (Ret $x) $s $b $i $h))
(= (cbpv:observe-tag VUnit $x $s $b $i $h) (W (Ret $x) $s $b $i $h))
(= (cbpv:observe-tag VClo $x $s $b $i $h) (W (Ret $x) $s $b $i $h))
(= (cbpv:observe-tag VSusp $x $s $b $i $h) (W (Ret $x) $s $b $i $h))
(= (cbpv:observe-ret (Ret $x) $s $b $i $h) (cbpv:observe $x $s $b $i $h))
(= (cbpv:observe-ret (Raised $k $p) $s $b $i $h) (W (Raised $k $p) $s $b $i $h))
(= (cbpv:observe-make $head (Ret $all) $s $b $i $h)
   (let (VList $vals) $all (W (Ret (VMake $head $vals)) $s $b $i $h)))
(= (cbpv:observe-make $head (Raised $k $p) $s $b $i $h) (W (Raised $k $p) $s $b $i $h))
(= (cbpv:observe-pair (Ret $all) $s $b $i $h)
   (let (VList ($p $q)) $all (W (Ret (VPair $p $q)) $s $b $i $h)))
(= (cbpv:observe-pair (Raised $k $p) $s $b $i $h) (W (Raised $k $p) $s $b $i $h))
; In order, each value observed in the world the previous one left.
(= (cbpv:observe-all $items $s $b $i $h)
   (if (== $items ()) (W (Ret (VList ())) $s $b $i $h)
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (let (W $t $s1 $b1 $i1 $h1) (cbpv:observe $first $s $b $i $h)
             (cbpv:observe-rest $t $rest $s1 $b1 $i1 $h1))))))
(= (cbpv:observe-rest (Ret $x) $rest $s $b $i $h)
   (let (W $t $s1 $b1 $i1 $h1) (cbpv:observe-all $rest $s $b $i $h)
     (cbpv:list-cons $x $t $s1 $b1 $i1 $h1)))
(= (cbpv:observe-rest (Raised $k $p) $rest $s $b $i $h) (W (Raised $k $p) $s $b $i $h))
(= (cbpv:list-cons $x (Ret $all) $s $b $i $h)
   (let (VList $xs) $all (let $ys (cons-atom $x $xs) (W (Ret (VList $ys)) $s $b $i $h))))
(= (cbpv:list-cons $x (Raised $k $p) $s $b $i $h) (W (Raised $k $p) $s $b $i $h))

(= (cbpv:comp $env (MapList $v $body) $s $b $i $h)
   (let $x (cbpv:value $env $v)
     (let (parts $elems) (cbpv:tuple-parts $x) (cbpv:map-elems $elems $env $body $s $b $i $h))))
(= (cbpv:map-elems $elems $env $body $s $b $i $h)
   (if (== $elems ()) (W (Ret (VList ())) $s $b $i $h)
       (let $first (car-atom $elems)
         (let $rest (cdr-atom $elems)
           (let (W $t $s1 $b1 $i1 $h1) (cbpv:comp (Cons $first $env) $body $s $b $i $h)
             (cbpv:map-next $t $rest $env $body $s1 $b1 $i1 $h1))))))
(= (cbpv:map-next (Ret $r) $rest $env $body $s $b $i $h)
   (let (W $t $s1 $b1 $i1 $h1) (cbpv:map-elems $rest $env $body $s $b $i $h)
     (cbpv:list-cons $r $t $s1 $b1 $i1 $h1)))
(= (cbpv:map-next (Raised $k $p) $rest $env $body $s $b $i $h) (W (Raised $k $p) $s $b $i $h))

(= (cbpv:comp $env (FoldList $v $init $body) $s $b $i $h)
   (let $x (cbpv:value $env $v)
     (let $a (cbpv:value $env $init)
       (let (parts $elems) (cbpv:tuple-parts $x) (cbpv:fold-elems $elems $a $env $body $s $b $i $h)))))
(= (cbpv:fold-elems $elems $acc $env $body $s $b $i $h)
   (if (== $elems ()) (W (Ret $acc) $s $b $i $h)
       (let $first (car-atom $elems)
         (let $rest (cdr-atom $elems)
           (let (W $t $s1 $b1 $i1 $h1) (cbpv:comp (Cons $first (Cons $acc $env)) $body $s $b $i $h)
             (cbpv:fold-next $t $rest $env $body $s1 $b1 $i1 $h1))))))
(= (cbpv:fold-next (Ret $r) $rest $env $body $s $b $i $h) (cbpv:fold-elems $rest $r $env $body $s $b $i $h))
(= (cbpv:fold-next (Raised $k $p) $rest $env $body $s $b $i $h) (W (Raised $k $p) $s $b $i $h))

; Prime's call-site default for an uninterpreted call, in the CBPV reading:
; the retention handler.
(= (cbpv:retain-inert $m) (Handle $m InertKind (Return (Var 0)) (Return (Var 0))))

; ---- The binding-site table
;
; One row per binding form of the draft's Prime syntax, read by the
; elaborator below:  (cbpv:site HEAD ARITY ROLES POLICY STRATEGY)
;   ROLES     (position role): pattern (binds the names written in it), pairs
;             (pattern/body pairs), source (the value a pattern consumes),
;             body (the scope of the names), binders (lexical parameters),
;             space, then, else, init, code.
;   POLICY    what a name written in a pattern position does by default:
;             fresh (a new slot per activation; the crossing set F{$x} makes
;             $x refer to the enclosing $x instead), refer (shares the
;             enclosing name), lexical (a parameter of the template, new per
;             activation), sealed (the quotation keeps the name to itself).
;   STRATEGY  how the CBPV reading binds a value there: value (`to`: before
;             the scope), need (a memo cell: at the first demand, shared),
;             name (a thunk: at every use), data (not evaluated), template
;             (run once per element), fresh-name (a new private name).
; The draft's C encodes the same facts in separate recognizers:
; prime_child_role, prime_migration_simple, prime_pattern_source,
; prime_iter_template, prime_lambda_telescope, prime_new_form and
; prime_code_like (src/prime_semantics.c, src/prime_scope_migration.inc).
(cbpv:site = 3 ((1 pattern) (2 body)) refer need)
(cbpv:site let 4 ((1 pattern) (2 source) (3 body)) fresh value)
(cbpv:site let* 3 ((1 pairs) (2 body)) fresh value)
(cbpv:site case 3 ((1 source) (2 pairs)) fresh value)
(cbpv:site switch 3 ((1 source) (2 pairs)) fresh value)
(cbpv:site switch-minimal 3 ((1 source) (2 pairs)) fresh value)
(cbpv:site match 4 ((1 space) (2 pattern) (3 body)) fresh value)
(cbpv:site chain 4 ((1 source) (2 pattern) (3 body)) fresh value)
(cbpv:site filter-atom 4 ((1 source) (2 pattern) (3 body)) fresh value)
(cbpv:site unify 5 ((1 pattern) (2 pattern) (3 then) (4 else)) refer data)
(cbpv:site lam 3 ((1 binders) (2 body)) lexical need)
(cbpv:site map-atom 4 ((1 source) (2 binders) (3 body)) lexical template)
(cbpv:site foldl-atom 6 ((1 source) (2 init) (3 binders) (4 binders) (5 body)) lexical template)
(cbpv:site new 3 ((1 binders) (2 body)) lexical fresh-name)
(cbpv:site quote 2 ((1 code)) sealed data)

(= (cbpv:site-strategy $head)
   (match &self (cbpv:site $head $arity $roles $policy $strategy) $strategy){$head})
(= (cbpv:site-policy $head)
   (match &self (cbpv:site $head $arity $roles $policy $strategy) $policy){$head})

; ---- Elaboration
;
; (cbpv:elaborated (quote E)) elaborates a program E into a CBPV term and
; runs it.  E is written with inert heads, so that no part of it can run as
; code while the elaborator reads it: a variable is (V name); the binding and control forms are s:let, s:superpose, s:empty,
; s:Error, s:if-error, s:demand (for demand:committed), s:licensed (for
; demand:licensed), ((s:lam (V x) B) A) for a lambda applied to one
; argument, s:quote for a quotation (data, as written) and the primitives
; s:+, s:-, s:*.  Definitions are facts: (cbpv:def NAME (PARAMS ...) BODY)
; and, for a symbol with equations, (cbpv:def-symbol NAME BODY).  A parameter
; is (V name) or one constructor pattern of variables; a datum such as a
; quotation is matched as its constructor applied to its parts.  The
; defaults are the table's: call and lambda arguments are need (the rows of
; = and lam), let is value.  (s:demand S E) changes the sites written inside
; E: eager makes arguments value; lazy makes let need; resample makes let
; name (call arguments stay one shared suspension); any other strategy
; leaves the bubble unevaluated.  Recursion is not elaborated.

(= (cbpv:default-mode)
   (let $args (cbpv:site-strategy =) (let $let (cbpv:site-strategy let) (Mode $args $let shared))))

; A name (a symbol with definitions) is produced once in a branch: every name
; is a memo cell bound around the query, forced where the name is read.
(= (cbpv:elaborated (quote $e))
   (let $mode (cbpv:default-mode)
     (let $names (collapse (match &self (cbpv:def-symbol $n $body) $n))
       (let $unique (unique-atom $names)
         (let $term (cbpv:bind-names $unique Nil $e $mode)
           (let $observed (Bind $term (Observe (Var 0)))
             (let $o (cbpv:outcomes $observed 0) (cbpv:answer-of $o))))))))

(= (cbpv:bind-names $names $scope $e $mode)
   (if (== $names ())
       (cbpv:elab $e $scope $mode)
       (let $name (car-atom $names)
         (let $rest (cdr-atom $names)
           (let $producer (cbpv:fresh-name $name)
             (let $body (cbpv:bind-names $rest (Cons ((name $name) need) $scope) $e $mode)
               (LetNeed $producer $body)))))))

; A fresh run of a name: a choice among its definitions.
(= (cbpv:fresh-name $s)
   (let $bodies (collapse (match &self (cbpv:def-symbol $s $body) $body){$s})
     (let $mode (cbpv:default-mode)
       (let $fresh (cbpv:with-names $mode fresh)
         (let $terms (cbpv:elab-all $bodies Nil $fresh) (cbpv:choice-of $terms))))))

(= (cbpv:with-names (Mode $args $let $old) $names) (Mode $args $let $names))
(= (cbpv:answer-of (Ok $v)) $v)
(= (cbpv:answer-of (Raised $k $p)) $p)
(= (cbpv:elaborated-bag (quote $e)) (sort-atom (collapse (cbpv:elaborated (quote $e)))))

; Per world: (Seen answer final-state intents), the intents in request order;
; the run starts from state 0.
(= (cbpv:elaborated-worlds (quote $e))
   (let $mode (cbpv:default-mode)
     (let $names (collapse (match &self (cbpv:def-symbol $n $body) $n))
       (let $unique (unique-atom $names)
         (let $term (cbpv:bind-names $unique Nil $e $mode)
           (let $observed (Bind $term (Observe (Var 0)))
             (let (World $b $o $s $is) (cbpv:run $observed 0)
               (let $a (cbpv:answer-of $o) (Seen $a $s $is)))))))))
(= (cbpv:elaborated-worlds-bag (quote $e)) (sort-atom (collapse (cbpv:elaborated-worlds (quote $e)))))

; Does the elaboration agree with an observed bag of answers?
(= (cbpv:agrees (quote $e) $observed)
   (let $mine (cbpv:elaborated-bag (quote $e))
     (let $theirs (sort-atom $observed) (== $mine $theirs))))

(= (cbpv:find (Cons ($n $k) $rest) $name)
   (if (== $n $name) (0 $k)
       (let ($i $k2) (cbpv:find $rest $name) (let $j (+ $i 1) ($j $k2)))))

(= (cbpv:ref $i $k)
   (if (== $k value) (Return (Var $i))
       (if (== $k retained) (Return (Var $i))
           (if (== $k demand) (Demand (Var $i)) (Force (Var $i))))))

(= (cbpv:push-blank $scope) (Cons (_ value) $scope))

(= (cbpv:elab $e $scope $mode)
   (let $meta (get-metatype $e)
     (if (== $meta Expression)
         (if (== $e ()) (Return (Const ()))
             (let $head (car-atom $e) (cbpv:elab-form $head $e $scope $mode)))
         (cbpv:elab-symbol $e $scope $mode))))

(= (cbpv:elab-symbol $s $scope (Mode $args $let $names))
   (let $bodies (collapse (match &self (cbpv:def-symbol $s $body) $body){$s})
     (if (== $bodies ()) (Return (Const $s))
         (if (== $names fresh)
             (cbpv:fresh-name $s)
             (let ($i $k) (cbpv:find $scope (name $s)) (cbpv:ref $i $k))))))

(= (cbpv:elab-all $items $scope $mode)
   (if (== $items ()) ()
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (let $t (cbpv:elab $first $scope $mode)
             (let $ts (cbpv:elab-all $rest $scope $mode) (cons-atom $t $ts)))))))

(= (cbpv:choice-of $terms)
   (if (== $terms ()) Zero
       (let $first (car-atom $terms)
         (let $rest (cdr-atom $terms)
           (if (== $rest ()) $first
               (let $more (cbpv:choice-of $rest) (Choose $first $more)))))))

(= (cbpv:elab-form $head $e $scope $mode)
   (if (cbpv:is-lambda $head) (cbpv:elab-beta $head $e $scope $mode)
   (if (== $head V) (let $name (index-atom $e 1) (let ($i $k) (cbpv:find $scope $name) (cbpv:ref $i $k)))
   (if (== $head s:quote) (let $code (index-atom $e 1) (let $q (cons-atom quote ($code)) (Return (Const $q))))
   (if (== $head s:let) (cbpv:elab-let-any $e $scope $mode)
   (if (== $head cross) (cbpv:elab-cross $e $scope $mode)
   (if (== $head s:superpose)
       (let $items (index-atom $e 1)
         (let $ts (cbpv:elab-all $items $scope $mode) (cbpv:choice-of $ts)))
   (if (== $head s:empty) Zero
   (if (== $head s:Error) (let $what (cdr-atom $e) (Raise ErrorKind (Const (Err $what))))
   (if (== $head s:if-error) (cbpv:elab-if-error $e $scope $mode)
   (if (== $head s:demand) (cbpv:elab-bubble $e $scope $mode)
   (if (== $head s:licensed) (cbpv:elab-licensed $e $scope $mode)
   (if (== $head s:+) (cbpv:elab-prim + $e $scope $mode)
   (if (== $head s:-) (cbpv:elab-prim - $e $scope $mode)
   (if (== $head s:*) (cbpv:elab-prim * $e $scope $mode)
       (cbpv:elab-more $head $e $scope $mode))))))))))))))))

; ((s:lam (V x) B) A): the lambda row of the table, a parameter bound by the
; argument strategy, as a call's is.  The body also sees the enclosing scope.
; One binder and one argument.
(= (cbpv:is-lambda $head)
   (let $meta (get-metatype $head)
     (if (== $meta Expression)
         (if (== $head ()) False (let $first (car-atom $head) (== $first s:lam)))
         False)))
(= (cbpv:elab-beta $lam $e $scope (Mode $args $let $names))
   (let $binder (index-atom $lam 1)
     (let $x (index-atom $binder 1)
       (let $body (index-atom $lam 2)
         (let $argument (index-atom $e 1)
           (let $a (cbpv:elab $argument $scope (Mode $args $let $names))
             (if (== $args value)
                 (let $b (cbpv:elab $body (Cons ($x value) $scope) (Mode $args $let $names)) (Bind $a $b))
                 (let $b (cbpv:elab $body (Cons ($x need) $scope) (Mode $args $let $names)) (LetNeed $a $b)))))))))

(= (cbpv:elab-let $e $scope (Mode $args $region $names))
   (let $pattern (index-atom $e 1)
   (let $x (index-atom $pattern 1)
     (let $value (index-atom $e 2)
       (let $body (index-atom $e 3)
         (let $let (cbpv:let-strategy $region $x $value $body)
         (if (== $let name)
             (let $v (cbpv:elab $value $scope (Mode $args $region fresh))
               (let $b (cbpv:elab $body (Cons ($x name) $scope) (Mode $args $region $names))
                 (App (Lam $b) (Thunk $v))))
             (let $v (cbpv:elab $value $scope (Mode $args $region $names))
               (if (== $let value)
                   (let $b (cbpv:elab $body (Cons ($x value) $scope) (Mode $args $region $names)) (Bind $v $b))
                   (let $b (cbpv:elab $body (Cons ($x need) $scope) (Mode $args $region $names)) (LetNeed $v $b)))))))))))

(= (cbpv:elab-if-error $e $scope $mode)
   (let $m (cbpv:elab (index-atom $e 1) $scope $mode)
     (let $blank (cbpv:push-blank $scope)
       (let $t (cbpv:elab (index-atom $e 2) $blank $mode)
         (let $f (cbpv:elab (index-atom $e 3) $blank $mode) (Handle $m ErrorKind $f $t))))))

(= (cbpv:elab-bubble $e $scope (Mode $args $let $names))
   (let $strategy (index-atom $e 1)
     (let $inner (index-atom $e 2)
       (if (== $strategy eager) (cbpv:elab $inner $scope (Mode value $let $names))
       (if (== $strategy lazy) (cbpv:elab $inner $scope (Mode $args need $names))
       (if (== $strategy resample) (cbpv:elab $inner $scope (Mode $args name $names))
           (Return (Const (demand:committed $strategy $inner)))))))))

; ---- Licensed bubbles
;
; (s:licensed S E) applies S at a binding site of E only where the answers
; are proved to agree with the default there.  With n the uses of the bound
; value and c the number of answers of its producer, eager against lazy, at
; an argument or at a let, agree exactly when 1 <= n or c = 1; resample
; against eager at a let agrees exactly when, besides, n <= 1 or c <= 1
; (GSLT/Dynamics/DemandAgreement: eagerShared_eq_lazyShared_iff and
; strategies_agree_iff).  cbpv:agree is the test `licensed` of
; TypeTheory/ContextualCBPVLicense, which holds exactly when the two
; strategies give the same answers, for every body that demands the value n
; times and builds its answer from the values (licensed_iff_map).  A call
; argument stays one shared suspension under resample.  n counts the occurrences of
; the variable in the body (a constructor parameter is demanded once; with
; several definitions, the fewest); c is counted for a producer without free
; variables by running it alone, and is taken to be 2 otherwise.  An inner
; binding of the same name is not told apart.
(= (cbpv:elab-licensed $e $scope (Mode $args $let $names))
   (let $strategy (index-atom $e 1)
     (let $inner (index-atom $e 2)
       (if (== $strategy eager) (cbpv:elab $inner $scope (Mode (license value) $let $names))
       (if (== $strategy lazy) (cbpv:elab $inner $scope (Mode $args (license need) $names))
       (if (== $strategy resample) (cbpv:elab $inner $scope (Mode $args (license name) $names))
           (Return (Const (demand:licensed $strategy $inner)))))))))

(= (cbpv:agree value $n $c) (or (>= $n 1) (== $c 1)))
(= (cbpv:agree need $n $c) (or (>= $n 1) (== $c 1)))
(= (cbpv:agree name $n $c) (and (or (>= $n 1) (== $c 1)) (or (<= $n 1) (<= $c 1))))

(= (cbpv:in-bubble? $strategy) (let $meta (get-metatype $strategy) (== $meta Expression)))

(= (cbpv:let-strategy $region $x $value $body)
   (if (cbpv:in-bubble? $region)
       (let (license $requested) $region
         (let $n (cbpv:uses $x $body)
           (let $c (cbpv:card $value)
             (if (cbpv:agree $requested $n $c) $requested value))))
       $region))

(= (cbpv:argument-strategy $strategy $argument $position $defs)
   (if (cbpv:in-bubble? $strategy)
       (let (license $requested) $strategy
         (let $n (cbpv:param-uses $position $defs)
           (let $c (cbpv:card $argument)
             (if (cbpv:agree $requested $n $c) $requested need))))
       $strategy))

(= (cbpv:uses $x $e)
   (let $meta (get-metatype $e)
     (if (== $meta Expression)
         (if (== $e ()) 0
             (let $head (car-atom $e)
               (if (== $head V)
                   (let $y (index-atom $e 1) (if (== $y $x) 1 0))
                   (cbpv:uses-all $x $e))))
         0)))
(= (cbpv:uses-all $x $items)
   (if (== $items ()) 0
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (let $a (cbpv:uses $x $first)
             (let $b (cbpv:uses-all $x $rest) (+ $a $b)))))))

(= (cbpv:param-uses $position $defs)
   (let ($params $body) (car-atom $defs)
     (let $rest (cdr-atom $defs)
       (let $i (- $position 1)
         (let $p (index-atom $params $i)
           (let $u (cbpv:param-use $p $body)
             (if (== $rest ()) $u
                 (let $v (cbpv:param-uses $position $rest) (if (< $u $v) $u $v)))))))))
(= (cbpv:param-use $p $body)
   (let $head (car-atom $p)
     (if (== $head V) (let $x (index-atom $p 1) (cbpv:uses $x $body)) 1)))

(= (cbpv:closed $e)
   (let $meta (get-metatype $e)
     (if (== $meta Expression)
         (if (== $e ()) True
             (let $head (car-atom $e) (if (== $head V) False (cbpv:all-closed $e))))
         True)))
(= (cbpv:all-closed $items)
   (if (== $items ()) True
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items) (if (cbpv:closed $first) (cbpv:all-closed $rest) False)))))

(= (cbpv:card $e)
   (if (cbpv:closed $e)
       (let $answers (collapse (cbpv:elaborated (quote $e){$e})) (size-atom $answers))
       2))

; The decisions of the bubbles in E, one record per binding site that binds a
; computation and whose strategy the bubble changes: (argument POSITION HEAD
; VERDICT) or (binding VERDICT).  Under a licensed bubble the verdict is
; applied or kept; under a committed one it is applied where the answers
; agree and changed where they may not, and a call argument under resample is
; kept.  The primitives demand their arguments.
(= (cbpv:license-report (quote $e)) (collapse (cbpv:sites $e none)))
(= (cbpv:sites $e $requested)
   (let $meta (get-metatype $e)
     (if (== $meta Expression)
         (if (== $e ()) (empty)
             (let $head (car-atom $e) (cbpv:sites-form $head $e $requested)))
         (empty))))
(= (cbpv:sites-all $items $requested)
   (if (== $items ()) (empty)
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (superpose ((cbpv:sites $first $requested) (cbpv:sites-all $rest $requested)))))))
(= (cbpv:sites-form $head $e $requested)
   (if (== $head s:licensed)
       (let $strategy (index-atom $e 1)
         (let $inner (index-atom $e 2)
           (let $r (cbpv:requested $strategy) (cbpv:sites $inner (license $r)))))
   (if (== $head s:demand)
       (let $strategy (index-atom $e 1)
         (let $inner (index-atom $e 2)
           (let $r (cbpv:requested $strategy) (cbpv:sites $inner (commit $r)))))
   (if (== $head s:let)
       (let $pattern (index-atom $e 1)
         (let $x (index-atom $pattern 1)
           (let $value (index-atom $e 2)
             (let $body (index-atom $e 3)
               (superpose ((cbpv:let-record $requested $x $value $body)
                           (cbpv:sites $value $requested)
                           (cbpv:sites $body $requested)))))))
   (if (cbpv:primitive? $head)
       (let $args (cdr-atom $e)
         (superpose ((cbpv:primitive-records $requested $head $args 1)
                     (cbpv:sites-all $args $requested))))
       (let $defs (collapse (match &self (cbpv:def $head $params $body) ($params $body)){$head})
         (let $args (cdr-atom $e)
           (if (== $defs ())
               (cbpv:sites-all $args $requested)
               (superpose ((cbpv:argument-records $requested $head $args 1 $defs)
                           (cbpv:sites-all $args $requested)))))))))))
(= (cbpv:requested eager) value)
(= (cbpv:requested lazy) need)
(= (cbpv:requested resample) name)

; The verdict at a site whose producer answers agree with the default or not.
(= (cbpv:verdict license True) applied)
(= (cbpv:verdict license False) kept)
(= (cbpv:verdict commit True) applied)
(= (cbpv:verdict commit False) changed)
(= (cbpv:primitive? $head) (if (== $head s:+) True (if (== $head s:-) True (== $head s:*))))

(= (cbpv:let-record $requested $x $value $body)
   (if (cbpv:in-bubble? $requested)
       (let ($bubble $r) $requested
         (if (== $r value) (empty)
             (let $n (cbpv:uses $x $body)
               (let $c (cbpv:card $value)
                 (let $agree (cbpv:agree $r $n $c)
                   (let $v (cbpv:verdict $bubble $agree) (binding $v)))))))
       (empty)))

; A literal argument binds nothing to commit; a computation is a call, a
; name or a variable.
(= (cbpv:computation? $a)
   (let $meta (get-metatype $a)
     (if (== $meta Expression) True
         (if (== $meta Symbol)
             (let $bodies (collapse (match &self (cbpv:def-symbol $a $body) $body){$a})
               (if (== $bodies ()) False True))
             False))))

(= (cbpv:argument-records $requested $head $args $position $defs)
   (if (== $args ()) (empty)
       (let $first (car-atom $args)
         (let $rest (cdr-atom $args)
           (let $next (+ $position 1)
             (superpose ((cbpv:argument-record $requested $head $first $position $defs)
                         (cbpv:argument-records $requested $head $rest $next $defs))))))))
(= (cbpv:argument-record $requested $head $argument $position $defs)
   (if (and (cbpv:in-bubble? $requested) (cbpv:computation? $argument))
       (let ($bubble $r) $requested
         (if (== $r value)
             (let $n (cbpv:param-uses $position $defs)
               (let $c (cbpv:card $argument)
                 (let $agree (cbpv:agree value $n $c)
                   (let $v (cbpv:verdict $bubble $agree) (argument $position $head $v)))))
             (if (== $r name) (argument $position $head kept) (empty))))
       (empty)))
(= (cbpv:primitive-records $requested $head $args $position)
   (if (== $args ()) (empty)
       (let $first (car-atom $args)
         (let $rest (cdr-atom $args)
           (let $next (+ $position 1)
             (superpose ((cbpv:primitive-record $requested $head $first $position)
                         (cbpv:primitive-records $requested $head $rest $next))))))))
(= (cbpv:primitive-record $requested $head $argument $position)
   (if (and (cbpv:in-bubble? $requested) (cbpv:computation? $argument))
       (let ($bubble $r) $requested
         (if (== $r value) (argument $position $head applied) (empty)))
       (empty)))

; A primitive demands both operands: a retained cell is forced here.
(= (cbpv:elab-prim $op $e $scope $mode)
   (let $x (cbpv:elab (index-atom $e 1) $scope $mode)
     (let $two (cbpv:push-blank (cbpv:push-blank $scope))
       (let $y (cbpv:elab (index-atom $e 2) $two $mode)
         (Bind $x (Bind (Demand (Var 0)) (Bind $y (Bind (Demand (Var 0))
           (Return (Prim2 $op (Var 2) (Var 0)))))))))))

; A head with definitions is a call; any other head builds data from its
; fields, each a pending computation (a memo cell) that a pattern or the
; answer boundary demands.
(= (cbpv:elab-application $head $e $scope $mode)
   (let $defs (collapse (match &self (cbpv:def $head $params $body) ($params $body)){$head})
     (let $args (cdr-atom $e)
       (if (== $defs ())
           (cbpv:elab-fields $head $args $scope $mode 0)
           (cbpv:elab-call $head $defs $args $scope $mode)))))

(= (cbpv:elab-fields $head $args $scope $mode $count)
   (if (== $args ())
       (let $vars (cbpv:field-vars $count) (Return (Make $head $vars)))
       (let $first (car-atom $args)
         (let $rest (cdr-atom $args)
           (let $t (cbpv:elab $first $scope $mode)
             (let $more (cbpv:elab-fields $head $rest (cbpv:push-blank $scope) $mode (+ $count 1))
               (LetNeed $t $more)))))))

; (Var (count - 1)) ... (Var 0): the fields in the order they were bound.
(= (cbpv:field-vars $count) (cbpv:field-vars-from $count ()))
(= (cbpv:field-vars-from $count $acc)
   (if (== $count 0) $acc
       (let $i (size-atom $acc)
         (cbpv:field-vars-from (- $count 1) (cons-atom (Var $i) $acc)))))

; A call binds its arguments by the mode's argument strategy, then chooses
; among the definitions; each definition reads the arguments through its
; parameters.
(= (cbpv:elab-call $head $defs $args $scope (Mode $argStrategy $let $names))
   (cbpv:bind-args $head $args $scope (Mode $argStrategy $let $names) () $defs))

(= (cbpv:bind-args $head $args $scope (Mode $argStrategy $let $names) $kinds $defs)
   (if (== $args ())
       (let $hidden (cbpv:hide-locals $scope)
         (let $count (size-atom $kinds)
           (let $base (cbpv:drop $hidden $count)
             (let $bodies (cbpv:def-terms $defs $kinds $base)
               (let $choice (cbpv:choice-of $bodies)
                 (let $all (cbpv:with-residual $head $defs $count $hidden $choice)
                   (cbpv:force-strict $defs $count $all)))))))
       (let $first (car-atom $args)
         (let $rest (cdr-atom $args)
           (let $t (cbpv:elab $first $scope (Mode $argStrategy $let $names))
             (let $position (+ (size-atom $kinds) 1)
             (let $k (cbpv:argument-strategy $argStrategy $first $position $defs)
             (let $kinds2 (cons-atom $k $kinds)
               (let $more (cbpv:bind-args $head $rest (cbpv:push-blank $scope) (Mode $argStrategy $let $names) $kinds2 $defs)
                 (if (== $k value) (Bind $t $more) (LetNeed $t $more)))))))))))

(= (cbpv:def-terms $defs $kinds $base)
   (if (== $defs ()) ()
       (let ($params $body) (car-atom $defs)
         (let $rest (cdr-atom $defs)
           (let $t (cbpv:def-term $params $body $kinds $base)
             (let $ts (cbpv:def-terms $rest $kinds $base) (cons-atom $t $ts)))))))

(= (cbpv:drop $scope $count)
   (if (== $count 0) $scope
       (let (Cons $entry $rest) $scope (cbpv:drop $rest (- $count 1)))))

; The callee sees its parameters and the names, not the caller's locals: at
; the call site every other entry of the scope is hidden.
(= (cbpv:hide-locals Nil) Nil)
(= (cbpv:hide-locals (Cons ($n $k) $rest))
   (let $more (cbpv:hide-locals $rest)
     (unify $n (name $x) (Cons ($n $k) $more) (Cons (_ value) $more))))

; $kinds lists the argument strategies innermost first.  Parameters that are
; distinct variables name the arguments; any other parameter is a pattern
; matched against its argument, and a variable named twice is compared.
(= (cbpv:def-term $params $body $kinds $base)
   (if (cbpv:linear-vars $params)
       (let $scope (cbpv:param-scope $params $kinds $base)
         (let $mode (cbpv:default-mode) (cbpv:elab $body $scope $mode)))
       (cbpv:def-term-general $params $body $kinds $base)))

; Parameters (p1 ... pn) with kinds (kn ... k1): the scope ((pn kn) ... (p1 k1)).
(= (cbpv:param-scope $params $kinds $base) (cbpv:param-scope-from $params $kinds $base))
(= (cbpv:param-scope-from $params $kinds $acc)
   (if (== $params ()) $acc
       (let (V $x) (car-atom $params)
         (let $count (size-atom $params)
           (let $k (cbpv:nth-from-end $kinds $count)
             (cbpv:param-scope-from (cdr-atom $params) $kinds (Cons ($x $k) $acc)))))))
(= (cbpv:nth-from-end $list $position)
   (let $index (- $position 1) (index-atom $list $index)))

(= (cbpv:is-var $p)
   (let $meta (get-metatype $p)
     (if (== $meta Expression)
         (if (== $p ()) False (let $head (car-atom $p) (let $size (size-atom $p) (and (== $head V) (== $size 2)))))
         False)))
(= (cbpv:var-name $p) (index-atom $p 1))

; Distinct variables only.
(= (cbpv:linear-vars $params) (cbpv:linear-vars-from $params ()))
(= (cbpv:linear-vars-from $params $seen)
   (if (== $params ()) True
       (let $first (car-atom $params)
         (if (cbpv:is-var $first)
             (let $x (cbpv:var-name $first)
               (if (cbpv:member $x $seen) False
                   (let $rest (cdr-atom $params)
                     (let $seen2 (cons-atom $x $seen) (cbpv:linear-vars-from $rest $seen2)))))
             False))))

(= (cbpv:member $x $items)
   (if (== $items ()) False
       (let $first (car-atom $items)
         (if (== $first $x) True (let $rest (cdr-atom $items) (cbpv:member $x $rest))))))

; The general case: the first occurrence of each variable parameter names its
; argument; the other parameters become obligations on their arguments.
(= (cbpv:def-term-general $params $body $kinds $base)
   (let $n (size-atom $params)
     (let $scope (cbpv:general-scope $params $kinds $base ())
       (let $bound (cbpv:param-bindings $params $n ())
         (let $obligs (cbpv:param-obligations $params $n () ())
           (let $mode (cbpv:default-mode)
             (cbpv:compile $obligs 0 () $bound $scope $mode fresh $body Zero)))))))
(= (cbpv:general-scope $params $kinds $acc $seen)
   (if (== $params ()) $acc
       (let $first (car-atom $params)
         (let $rest (cdr-atom $params)
           (let $count (size-atom $params)
             (let $k (cbpv:nth-from-end $kinds $count)
               (if (cbpv:is-var $first)
                   (let $x (cbpv:var-name $first)
                     (if (cbpv:member $x $seen)
                         (cbpv:general-scope $rest $kinds (Cons (_arg $k) $acc) $seen)
                         (let $seen2 (cons-atom $x $seen)
                           (cbpv:general-scope $rest $kinds (Cons ($x $k) $acc) $seen2))))
                   (cbpv:general-scope $rest $kinds (Cons (_arg $k) $acc) $seen))))))))
; The first occurrence of each variable parameter: (x (Outer j)).
(= (cbpv:param-bindings $params $n $acc)
   (if (== $params ()) $acc
       (let $first (car-atom $params)
         (let $rest (cdr-atom $params)
           (let $count (size-atom $params)
             (let $j (- $count 1)
               (if (cbpv:is-var $first)
                   (let $x (cbpv:var-name $first)
                     (if (cbpv:bound-has $acc $x)
                         (cbpv:param-bindings $rest $n $acc)
                         (let $acc2 (cons-atom ($x (Outer $j)) $acc) (cbpv:param-bindings $rest $n $acc2))))
                   (cbpv:param-bindings $rest $n $acc))))))))
; Every parameter that is not the first occurrence of a variable.
(= (cbpv:param-obligations $params $n $seen $acc)
   (if (== $params ()) $acc
       (let $first (car-atom $params)
         (let $rest (cdr-atom $params)
           (let $count (size-atom $params)
             (let $j (- $count 1)
               (if (and (cbpv:is-var $first) (not (cbpv:member (cbpv:var-name $first) $seen)))
                   (let $x (cbpv:var-name $first)
                     (let $seen2 (cons-atom $x $seen) (cbpv:param-obligations $rest $n $seen2 $acc)))
                   (let $o ($first (Outer $j))
                     (let $acc2 (cbpv:snoc $acc $o) (cbpv:param-obligations $rest $n $seen $acc2))))))))))

(= (cbpv:snoc $items $x) (let $one ($x) (cbpv:append $items $one)))
(= (cbpv:append $front $back)
   (if (== $front ()) $back
       (let $first (car-atom $front)
         (let $rest (cdr-atom $front)
           (let $more (cbpv:append $rest $back) (cons-atom $first $more))))))
(= (cbpv:bound-has $bound $x)
   (if (== $bound ()) False
       (let ($y $r) (car-atom $bound)
         (if (== $y $x) True (let $rest (cdr-atom $bound) (cbpv:bound-has $rest $x))))))
(= (cbpv:bound-ref $bound $x)
   (if (== $bound ()) none
       (let ($y $r) (car-atom $bound)
         (if (== $y $x) $r (let $rest (cdr-atom $bound) (cbpv:bound-ref $rest $x))))))

; ---- Patterns
;
; A pattern is (V x), a constant (a symbol, a number, a string or ()), or an
; expression of patterns, matched element by element against an expression
; value of the same length.  Matching works through a list of obligations
; (PATTERN REF): REF is (Slot p), the p-th entry pushed since the match began,
; or (Outer j), entry j of the scope it began in.  Inspecting a value demands
; it first; a variable binds what is there, demanded or not, and reading the
; variable later gives that retained value without demanding it.  A variable this
; match has bound already is compared, and so is a variable the policy makes
; refer to the enclosing binding: refer (unify), or (pinned NAMES POLICY) for
; a crossing set.  On success the body runs in the scope extended by every
; entry pushed; on failure `else`, a term of the scope the match began in.
(= (cbpv:ref-at (Slot $p) $depth) (let $i (- (- $depth 1) $p) (Var $i)))
(= (cbpv:ref-at (Outer $j) $depth) (let $i (+ $j $depth) (Var $i)))

(= (cbpv:compile $obligs $depth $pushed $bound $base $mode $policy $body $else)
   (if (== $obligs ())
       (if (== $body cbpv:matched) Zero
           (let $scope (cbpv:prepend $pushed $base) (cbpv:elab $body $scope $mode)))
       (let $first (car-atom $obligs)
         (let $rest (cdr-atom $obligs)
           (let ($pattern $ref) $first
             (cbpv:compile-one $pattern $ref $rest $depth $pushed $bound $base $mode $policy $body $else))))))

(= (cbpv:compile-one $pattern $ref $rest $depth $pushed $bound $base $mode $policy $body $else)
   (if (cbpv:is-var $pattern)
       (cbpv:compile-variable $pattern $ref $rest $depth $pushed $bound $base $mode $policy $body $else)
       (let $meta (get-metatype $pattern)
         (if (and (== $meta Expression) (not (== $pattern ())))
             (cbpv:compile-tuple $pattern $ref $rest $depth $pushed $bound $base $mode $policy $body $else)
             (cbpv:compile-constant $pattern $ref $rest $depth $pushed $bound $base $mode $policy $body $else)))))

(= (cbpv:compile-variable $pattern $ref $rest $depth $pushed $bound $base $mode $policy $body $else)
   (let $x (cbpv:var-name $pattern)
     (let $earlier (cbpv:bound-ref $bound $x)
       (if (== $earlier none)
           (let $outer (cbpv:refers $policy $x $base)
             (if (== $outer none)
                 (let $here (cbpv:ref-at $ref $depth)
                   (let $depth2 (+ $depth 1)
                     (let $entry ($x retained)
                       (let $pushed2 (cons-atom $entry $pushed)
                         (let $mine ($x (Slot $depth))
                           (let $bound2 (cons-atom $mine $bound)
                             (let $k (cbpv:compile $rest $depth2 $pushed2 $bound2 $base $mode $policy $body $else)
                               (Bind (Return $here) $k))))))))
                 (cbpv:compile-compare $ref $outer $rest $depth $pushed $bound $base $mode $policy $body $else)))
           (cbpv:compile-compare $ref $earlier $rest $depth $pushed $bound $base $mode $policy $body $else)))))

; Observe both values, every cell inside them demanded, then compare them.
(= (cbpv:compile-compare $ref $other $rest $depth $pushed $bound $base $mode $policy $body $else)
   (let $a (cbpv:ref-at $ref $depth)
     (let $d1 (+ $depth 1)
       (let $b (cbpv:ref-at $other $d1)
         (let $d2 (+ $depth 2)
           (let $pushed2 (cbpv:push-blanks 2 $pushed)
             (let $k (cbpv:compile $rest $d2 $pushed2 $bound $base $mode $policy $body $else)
               (let $fail (Drop $d2 $else)
                 (Bind (Observe $a) (Bind (Observe $b) (IfEq (Var 1) (Var 0) $k $fail)))))))))))

(= (cbpv:compile-constant $pattern $ref $rest $depth $pushed $bound $base $mode $policy $body $else)
   (let $a (cbpv:ref-at $ref $depth)
     (let $d1 (+ $depth 1)
       (let $pushed2 (cbpv:push-blanks 1 $pushed)
         (let $k (cbpv:compile $rest $d1 $pushed2 $bound $base $mode $policy $body $else)
           (let $fail (Drop $d1 $else)
             (let $c (Const $pattern)
               (Bind (Demand $a) (IfEq (Var 0) $c $k $fail)))))))))

(= (cbpv:compile-tuple $pattern $ref $rest $depth $pushed $bound $base $mode $policy $body $else)
   (let $a (cbpv:ref-at $ref $depth)
     (let $len (size-atom $pattern)
       (let $d1 (+ $depth 1)
         (let $subs (cbpv:element-obligations $pattern $d1)
           (let $obligs (cbpv:append $subs $rest)
             (let $d2 (+ $d1 $len)
               (let $count (+ $len 1)
                 (let $pushed2 (cbpv:push-blanks $count $pushed)
                   (let $k (cbpv:compile $obligs $d2 $pushed2 $bound $base $mode $policy $body $else)
                     (let $fail (Drop $d1 $else)
                       (Bind (Demand $a) (CaseTuple (Var 0) $len $k $fail)))))))))))))

(= (cbpv:element-obligations $elements $slot)
   (if (== $elements ()) ()
       (let $first (car-atom $elements)
         (let $rest (cdr-atom $elements)
           (let $next (+ $slot 1)
             (let $more (cbpv:element-obligations $rest $next)
               (let $o ($first (Slot $slot)) (cons-atom $o $more))))))))

(= (cbpv:push-blanks $n $pushed)
   (if (== $n 0) $pushed
       (let $m (- $n 1) (let $more (cons-atom (_ value) $pushed) (cbpv:push-blanks $m $more)))))
(= (cbpv:prepend $pushed $scope)
   (if (== $pushed ()) $scope
       (let $first (car-atom $pushed)
         (let $rest (cdr-atom $pushed)
           (let $inner (cbpv:prepend $rest $scope) (Cons $first $inner))))))

; Does a variable of this pattern refer to the enclosing binding?
(= (cbpv:refers $policy $x $base)
   (if (cbpv:policy-refers $policy $x) (cbpv:find-or-none $base $x) none))
(= (cbpv:policy-refers $policy $x)
   (let $meta (get-metatype $policy)
     (if (== $meta Expression)
         (let (pinned $names $inner) $policy
           (if (cbpv:member $x $names) True (cbpv:policy-refers $inner $x)))
         (== $policy refer))))
(= (cbpv:find-or-none Nil $name) none)
(= (cbpv:find-or-none (Cons ($n $k) $rest) $name)
   (if (== $n $name) (Outer 0)
       (let $r (cbpv:find-or-none $rest $name) (cbpv:shift-outer $r))))
(= (cbpv:shift-outer $r)
   (if (== $r none) none (let (Outer $j) $r (let $j2 (+ $j 1) (Outer $j2)))))

; ---- The binding forms
;
; Each reads its row of the binding-site table: the strategy decides how the
; source is computed (value: before the clauses; data: not computed;
; template: once per element), the policy what a variable written in a
; pattern does.  (cross (x ...) FORM) pins the names for FORM's patterns.
(= (cbpv:site-of s:case) case)
(= (cbpv:site-of s:switch) switch)
(= (cbpv:site-of s:switch-minimal) switch-minimal)
(= (cbpv:site-of s:match) match)
(= (cbpv:site-of s:chain) chain)
(= (cbpv:site-of s:unify) unify)
(= (cbpv:site-of s:let) let)
(= (cbpv:site-of s:map-atom) map-atom)
(= (cbpv:site-of s:foldl-atom) foldl-atom)
(= (cbpv:policy-of $head) (let $site (cbpv:site-of $head) (cbpv:site-policy $site)))

(= (cbpv:binding-form? $head)
   (cbpv:member $head (s:case s:switch s:switch-minimal s:match s:chain s:unify s:let)))

(= (cbpv:elab-binding $head $e $scope $mode $policy)
   (if (cbpv:member $head (s:case s:switch s:switch-minimal)) (cbpv:elab-case $e $scope $mode $policy)
   (if (== $head s:match) (cbpv:elab-match $e $scope $mode $policy)
   (if (== $head s:chain) (cbpv:elab-chain $e $scope $mode $policy)
   (if (== $head s:unify) (cbpv:elab-unify $e $scope $mode $policy)
       (cbpv:elab-let-pattern $e $scope $mode $policy))))))

; (cross (x ...) FORM)
(= (cbpv:elab-cross $e $scope $mode)
   (let $names (index-atom $e 1)
     (let $form (index-atom $e 2)
       (let $head (car-atom $form)
         (if (cbpv:binding-form? $head)
             (let $inner (cbpv:policy-of $head)
               (let $policy (pinned $names $inner) (cbpv:elab-binding $head $form $scope $mode $policy)))
             (cbpv:elab $form $scope $mode))))))

(= (cbpv:elab-let-any $e $scope $mode)
   (let $pattern (index-atom $e 1)
     (if (cbpv:is-var $pattern)
         (cbpv:elab-let $e $scope $mode)
         (let $policy (cbpv:policy-of s:let) (cbpv:elab-let-pattern $e $scope $mode $policy)))))

; A clause list (case, switch): the source by value, then the first clause
; whose pattern matches; no clause, no world.
(= (cbpv:elab-case $e $scope $mode $policy)
   (let $source (index-atom $e 1)
     (let $clauses (index-atom $e 2)
       (let $m (cbpv:elab $source $scope $mode)
         (let $base (Cons (_ value) $scope)
           (let $k (cbpv:clauses $clauses $base $mode $policy) (Bind $m $k)))))))
(= (cbpv:clauses $clauses $base $mode $policy)
   (if (== $clauses ()) Zero
       (let $first (car-atom $clauses)
         (let $rest (cdr-atom $clauses)
           (let ($pattern $body) $first
             (let $else (cbpv:clauses $rest $base $mode $policy)
               (let $o ($pattern (Outer 0))
                 (let $obligs ($o)
                   (cbpv:compile $obligs 0 () () $base $mode $policy $body $else)))))))))

; (s:match SPACE PATTERN BODY) over the facts (cbpv:fact SPACE ATOM): a choice
; among the facts whose atom matches.
(= (cbpv:elab-match $e $scope $mode $policy)
   (let $space (index-atom $e 1)
     (let $pattern (index-atom $e 2)
       (let $body (index-atom $e 3)
         (let $facts (collapse (match &self (cbpv:fact $space $atom) $atom){$space})
           (let $base (Cons (_ value) $scope)
             (let $terms (cbpv:fact-terms $facts $pattern $body $base $mode $policy)
               (cbpv:choice-of $terms))))))))
(= (cbpv:fact-terms $facts $pattern $body $base $mode $policy)
   (if (== $facts ()) ()
       (let $fact (car-atom $facts)
         (let $rest (cdr-atom $facts)
           (let $o ($pattern (Outer 0))
             (let $obligs ($o)
               (let $k (cbpv:compile $obligs 0 () () $base $mode $policy $body Zero)
                 (let $t (Bind (Return (Const $fact)) $k)
                   (let $ts (cbpv:fact-terms $rest $pattern $body $base $mode $policy)
                     (cons-atom $t $ts))))))))))

; (s:chain SOURCE PATTERN BODY), and s:let with a pattern: the source by
; value, then the pattern; no match, no world.
(= (cbpv:elab-chain $e $scope $mode $policy)
   (let $source (index-atom $e 1)
     (let $pattern (index-atom $e 2)
       (let $body (index-atom $e 3)
         (cbpv:bind-pattern $source $pattern $body $scope $mode $policy)))))
(= (cbpv:elab-let-pattern $e $scope $mode $policy)
   (let $pattern (index-atom $e 1)
     (let $source (index-atom $e 2)
       (let $body (index-atom $e 3)
         (cbpv:bind-pattern $source $pattern $body $scope $mode $policy)))))
(= (cbpv:bind-pattern $source $pattern $body $scope $mode $policy)
   (let $m (cbpv:elab $source $scope $mode)
     (let $base (Cons (_ value) $scope)
       (let $o ($pattern (Outer 0))
         (let $obligs ($o)
           (let $k (cbpv:compile $obligs 0 () () $base $mode $policy $body Zero) (Bind $m $k)))))))

; (s:unify A B THEN ELSE): neither side is computed.  The side with variables
; the scope does not bind is the pattern; a variable the scope binds refers to
; its value (the table's refer).  Unbound variables on both sides are not
; elaborated.
(= (cbpv:elab-unify $e $scope $mode $policy)
   (let $left (index-atom $e 1)
     (let $right (index-atom $e 2)
       (let $then (index-atom $e 3)
         (let $otherwise (index-atom $e 4)
           (let $else (cbpv:elab $otherwise $scope $mode)
             (if (cbpv:closed-in $right $scope)
                 (cbpv:unify-with $left $right $then $else $scope $mode $policy)
                 (cbpv:unify-with $right $left $then $else $scope $mode $policy))))))))
(= (cbpv:unify-with $pattern $data $then $else $scope $mode $policy)
   (let $m (cbpv:elab-datum $data $scope)
     (let $base (Cons (_ value) $scope)
       (let $o ($pattern (Outer 0))
         (let $obligs ($o)
           (let $fail (Drop 1 $else)
             (let $k (cbpv:compile $obligs 0 () () $base $mode $policy $then $fail) (Bind $m $k))))))))
; Every variable of the expression is bound in the scope.
(= (cbpv:closed-in $e $scope)
   (if (cbpv:is-var $e)
       (let $x (cbpv:var-name $e) (let $r (cbpv:find-or-none $scope $x) (not (== $r none))))
       (let $meta (get-metatype $e)
         (if (and (== $meta Expression) (not (== $e ())))
             (cbpv:all-closed-in $e $scope)
             True))))
(= (cbpv:all-closed-in $items $scope)
   (if (== $items ()) True
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (if (cbpv:closed-in $first $scope) (cbpv:all-closed-in $rest $scope) False)))))
; Data, not computed: a variable is its value; anything else is as written.
(= (cbpv:elab-datum $d $scope)
   (if (cbpv:is-var $d)
       (let $x (cbpv:var-name $d) (let ($i $k) (cbpv:find $scope $x) (cbpv:ref $i $k)))
       (let $meta (get-metatype $d)
         (if (and (== $meta Expression) (not (== $d ())))
             (if (cbpv:closed $d) (Return (Const $d)) (cbpv:datum-elements $d $scope 0))
             (Return (Const $d))))))
(= (cbpv:datum-elements $items $scope $count)
   (if (== $items ())
       (let $vars (cbpv:field-vars $count) (Return (List $vars)))
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (let $t (cbpv:elab-datum $first $scope)
             (let $next (+ $count 1)
               (let $more (cbpv:datum-elements $rest (cbpv:push-blank $scope) $next) (Bind $t $more))))))))

; (s:map-atom LIST (V x) BODY), (s:foldl-atom LIST INIT (V acc) (V x) BODY):
; the list by value, the body once per element (the template strategy).
(= (cbpv:elab-map $e $scope $mode)
   (let $list (index-atom $e 1)
     (let $binder (index-atom $e 2)
       (let $x (cbpv:var-name $binder)
         (let $body (index-atom $e 3)
           (let $m (cbpv:elab $list $scope $mode)
             (let $inner (Cons ($x retained) (Cons (_ value) $scope))
               (let $b (cbpv:elab $body $inner $mode) (Bind $m (MapList (Var 0) $b))))))))))
(= (cbpv:elab-fold $e $scope $mode)
   (let $list (index-atom $e 1)
     (let $init (index-atom $e 2)
       (let $accb (index-atom $e 3)
         (let $acc (cbpv:var-name $accb)
           (let $xb (index-atom $e 4)
             (let $x (cbpv:var-name $xb)
               (let $body (index-atom $e 5)
                 (let $m (cbpv:elab $list $scope $mode)
                   (let $i (cbpv:elab $init (Cons (_ value) $scope) $mode)
                     (let $inner (Cons ($x retained) (Cons ($acc retained) (Cons (_ value) (Cons (_ value) $scope))))
                       (let $b (cbpv:elab $body $inner $mode)
                         (Bind $m (Bind $i (FoldList (Var 1) (Var 0) $b)))))))))))))))

; ---- Canonical application: (App F A), (Lam T B), (idx i)
;
; A Lam is a closure whose binder is one shared suspension, demanded where
; (idx i) reads it; App binds its argument so, whatever F computes.  An App
; whose function is not a lambda, an application or a call stays as written.
(= (cbpv:elab-abt-lam $e $scope $mode)
   (let $body (index-atom $e 2)
     (let $inner (Cons (abt need) $scope)
       (let $b (cbpv:elab $body $inner $mode) (Return (Thunk (Lam $b)))))))
(= (cbpv:elab-abt-app $e $scope $mode)
   (let $f (index-atom $e 1)
     (let $a (index-atom $e 2)
       (if (cbpv:abt-function? $f)
           (let $fm (cbpv:elab $f $scope $mode)
             (let $am (cbpv:elab $a (Cons (_ value) $scope) $mode)
               (Bind $fm (LetNeed $am (App (Force (Var 1)) (Var 0))))))
           (Return (Const $e))))))
(= (cbpv:abt-function? $f)
   (let $meta (get-metatype $f)
     (if (and (== $meta Expression) (not (== $f ())))
         (let $head (car-atom $f)
           (if (cbpv:member $head (Lam App V))
               True
               (let $defs (collapse (match &self (cbpv:def $head $params $body) $head){$head})
                 (not (== $defs ())))))
         False)))
(= (cbpv:elab-idx $e $scope $mode)
   (let $n (index-atom $e 1)
     (let $found (cbpv:find-abt $scope $n)
       (if (== $found none) (Return (Const $e)) (let (Outer $j) $found (Force (Var $j)))))))
(= (cbpv:find-abt Nil $n) none)
(= (cbpv:find-abt (Cons ($name $k) $rest) $n)
   (if (== $name abt)
       (if (== $n 0) (Outer 0)
           (let $m (- $n 1) (let $r (cbpv:find-abt $rest $m) (cbpv:shift-outer $r))))
       (let $r (cbpv:find-abt $rest $n) (cbpv:shift-outer $r))))

; ---- Effects, explicit suspensions and divergence
;
; (s:print X) records X's value as a deferred intent and answers (); (s:get-state)
; reads the state; (s:change-state! E) writes E's value and answers ();
; (s:delay E) is an explicit suspension, (s:force E) forces E's value;
; An expression whose first element is itself an expression is a tuple of
; its elements, each pending, as constructor fields are.
(= (cbpv:expression? $x)
   (let $meta (get-metatype $x) (if (== $meta Expression) (not (== $x ())) False)))
(= (cbpv:elab-tuple $items $scope $mode $count)
   (if (== $items ())
       (let $vars (cbpv:field-vars $count) (Return (List $vars)))
       (let $first (car-atom $items)
         (let $rest (cdr-atom $items)
           (let $t (cbpv:elab $first $scope $mode)
             (let $next (+ $count 1)
               (let $more (cbpv:elab-tuple $rest (cbpv:push-blank $scope) $mode $next) (LetNeed $t $more))))))))

; (s:diverge) stands for a computation without an answer: it raises the kind
; Diverged, so a demand of it shows instead of hanging.  Divergence itself
; needs recursion, which is not part of this calculus.  (s:datum D) is D as
; data, computed by nothing (what cons-atom earns).
(= (cbpv:elab-more $head $e $scope $mode)
   (if (cbpv:binding-form? $head)
       (let $policy (cbpv:policy-of $head) (cbpv:elab-binding $head $e $scope $mode $policy))
   (if (== $head s:map-atom) (cbpv:elab-map $e $scope $mode)
   (if (== $head s:foldl-atom) (cbpv:elab-fold $e $scope $mode)
   (if (cbpv:abt-form? $head $e Lam 3) (cbpv:elab-abt-lam $e $scope $mode)
   (if (cbpv:abt-form? $head $e App 3) (cbpv:elab-abt-app $e $scope $mode)
   (if (cbpv:abt-form? $head $e idx 2) (cbpv:elab-idx $e $scope $mode)
   (if (== $head s:print)
       (let $what (index-atom $e 1)
         (let $m (cbpv:elab $what $scope $mode)
           (Bind $m (Bind (Demand (Var 0)) (Intent (Var 0) (Return (Const ())))))))
   (if (== $head s:get-state) (Read (Return (Var 0)))
   (if (== $head s:change-state!)
       (let $v (index-atom $e 1)
         (let $m (cbpv:elab $v $scope $mode) (Bind $m (Write (Var 0) (Return (Const ()))))))
   (if (== $head s:delay)
       (let $src (index-atom $e 1) (let $m (cbpv:elab $src $scope $mode) (Return (Delay $src $m))))
   (if (== $head s:force)
       (let $v (index-atom $e 1) (let $m (cbpv:elab $v $scope $mode) (Bind $m (Force (Var 0)))))
   (if (== $head s:diverge) (Raise Diverged (Const omega))
   (if (== $head s:datum) (let $d (index-atom $e 1) (Return (Const $d)))
   (if (cbpv:expression? $head) (cbpv:elab-tuple $e $scope $mode 0)
       (cbpv:elab-application $head $e $scope $mode))))))))))))))))
(= (cbpv:abt-form? $head $e $word $len)
   (if (== $head $word) (let $size (size-atom $e) (== $size $len)) False))

; ---- Calls: strict positions and the unmatched call
;
; A position where every definition has a pattern is demanded once, before
; the definitions are tried, so they share one demand of that argument.  When
; no definition is total, a call none of them matches stays as written: the
; call's datum, its arguments observed at the answer boundary.
(= (cbpv:force-strict $defs $count $term) (cbpv:force-positions $defs $count 1 $term))
(= (cbpv:force-positions $defs $count $position $term)
   (if (> $position $count) $term
       (let $next (+ $position 1)
         (let $inner (cbpv:force-positions $defs $count $next $term)
           (if (cbpv:strict-at $defs $position)
               (let $i (- $count $position) (Bind (Demand (Var $i)) (Drop 1 $inner)))
               $inner)))))
(= (cbpv:strict-at $defs $position)
   (if (== $defs ()) True
       (let ($params $body) (car-atom $defs)
         (let $j (- $position 1)
           (let $p (index-atom $params $j)
             (if (cbpv:is-var $p) False (let $rest (cdr-atom $defs) (cbpv:strict-at $rest $position))))))))

(= (cbpv:with-residual $head $defs $count $scope $choice)
   (if (cbpv:some-total $defs) $choice
       (let $tests (cbpv:residual-tests $head $defs $count $scope) (Choose $choice $tests))))
(= (cbpv:some-total $defs)
   (if (== $defs ()) False
       (let ($params $body) (car-atom $defs)
         (if (cbpv:linear-vars $params) True (let $rest (cdr-atom $defs) (cbpv:some-total $rest))))))
(= (cbpv:residual-tests $head $defs $count $scope)
   (if (== $defs ())
       (let $vars (cbpv:field-vars $count) (Return (Make $head $vars)))
       (let ($params $body) (car-atom $defs)
         (let $rest (cdr-atom $defs)
           (let $else (cbpv:residual-tests $head $rest $count $scope)
             (let $obligs (cbpv:all-obligations $params $count)
               (let $mode (cbpv:default-mode)
                 (cbpv:compile $obligs 0 () () $scope $mode fresh cbpv:matched $else))))))))
(= (cbpv:all-obligations $params $count)
   (if (== $params ()) ()
       (let $first (car-atom $params)
         (let $rest (cdr-atom $params)
           (let $size (size-atom $params)
             (let $j (- $size 1)
               (let $o ($first (Outer $j))
                 (let $more (cbpv:all-obligations $rest $count) (cons-atom $o $more)))))))))
