; Binding cases over read TPTP text: shadowing, capture, typed telescopes,
; $let and #, renaming apart, alpha equality, and two records using X.  The
; user rewrites below match the formulas; binding operations take formulas
; as data, so none of them fires.

(= (p $x) BOOM)
(= (lives $x) True)
(= (q $x $y) (q $y $x))

(= (binding-test:formula $record)
   (case $record
     (((fof $_n $_r $f) $f)
      ((cnf $_n $_r $f) $f)
      ((tff $_n $_r $f) $f)
      ((thf $_n $_r $f) $f)
      ($other (NotAFormula $other)))))

(= (binding-test:formulas $policy $text)
   (let $records (tptp:read-text (tptp:open-reader strict) $policy $text)
     (binding-test:map-formula $records)))
(= (binding-test:map-formula $records)
   (if (== (size-atom $records) 0)
       $records
       (let $first (car-atom $records)
         (let $rest (cdr-atom $records)
           (let $head (binding-test:formula $first)
             (let $tail (binding-test:map-formula $rest)
               (cons-atom $head $tail)))))))

(= (binding-test:first $policy $text)
   (let $fs (binding-test:formulas $policy $text) (car-atom $fs)))

(= (binding-test:free $policy $text)
   (let $f (binding-test:first $policy $text)
     (BindingV1 free $policy (tptp:free-variables $policy $f))))

(= (binding-test:substitute $policy $bindings $text)
   (let $b $bindings
     (let $f (binding-test:first $policy $text)
       (let $g (tptp:substitute $policy $b $f)
         (BindingV1 substitute $policy $g)))))

(= (binding-test:apart $policy $avoid $text)
   (let $f (binding-test:first $policy $text)
     (let $g (tptp:rename-apart $policy $avoid $f)
       (BindingV1 apart $policy $g))))

(= (binding-test:alpha $policy $text)
   (let [$left $right] (binding-test:formulas $policy $text)
     (BindingV1 alpha $policy (tptp:alpha-equal $policy $left $right))))

; Each record binds its own variables: a clause's universal closure is the
; formula whose alpha equality compares two records.
(= (binding-test:closure $policy $f)
   (let $vs (tptp:free-variables $policy $f) (∀ $vs $f)))

(= (binding-test:records $policy $text)
   (let $fs (binding-test:formulas $policy $text)
     (let [$left $right] $fs
       (let $left-closed (binding-test:closure $policy $left)
         (let $right-closed (binding-test:closure $policy $right)
           (BindingV1 records $policy
             (tptp:free-variables $policy $left)
             (tptp:free-variables $policy $right)
             (tptp:alpha-equal $policy $left $right)
             (tptp:alpha-equal $policy $left-closed $right-closed)))))))

(= (binding-test:all $policy)
   (let $a (binding-test:free $policy "fof(shadow,axiom,![X]:(p(X) & ?[X]:q(X,Y))).")
   (let $b (binding-test:free $policy "cnf(clause,axiom,~lives(X) | q(X,Y)).")
   (let $c (binding-test:free $policy "thf(ho,axiom,F @ (^[X:$i]: (G @ X))).")
   (let $d (binding-test:free $policy "thf(dep,axiom,![A:$tType,X:A]: (X = B)).")
   (let $e (binding-test:free $policy "tff(hash,axiom,#[X:$i]: p(X,W)).")
   (let $f (binding-test:free $policy "tff(let,axiom,![Y:$i]: $let(f: $i > $i, f(X) := g(X,Y), p(f(Z)))).")
   (let $g (binding-test:substitute $policy (binding-test:bindings $policy bind-capture)
              "fof(capture,axiom,![X]: p(X,Y)).")
   (let $h (binding-test:substitute $policy (binding-test:bindings $policy bind-shadowed)
              "fof(shadowed,axiom,p(X) & ![X]: p(X)).")
   (let $i (binding-test:substitute $policy (binding-test:bindings $policy bind-telescope)
              "thf(telescope,axiom,![A:$tType,X:A]: (X = B)).")
   (let $j (binding-test:substitute $policy (binding-test:bindings $policy bind-constant)
              "tff(constant,axiom,$let(c: $i, c := a, p(c,Z))).")
   (let $k (binding-test:apart $policy () "fof(twice,axiom,(![X]: p(X)) & (![X]: lives(X))).")
   (let $l (binding-test:apart $policy () "fof(free,axiom,p(X) & ![X]: p(X)).")
   (let $m (binding-test:alpha $policy "fof(l,axiom,![X]: p(X)). fof(r,axiom,![Y]: p(Y)).")
   (let $n (binding-test:alpha $policy "fof(l,axiom,![X,Y]: q(X,Y)). fof(r,axiom,![Y,X]: q(X,Y)).")
   (let $o (binding-test:records $policy "cnf(c1,axiom,p(X) | ~lives(X)). cnf(c2,axiom,p(Y) | ~lives(Y)).")
   (let $r (binding-test:alpha $policy "tff(l,axiom,$let(c: $i, c := c, p(c))). tff(r,axiom,$let(d: $i, d := c, p(d))).")
     (BindingV1Results $policy $a $b $c $d $e $f $g $h $i $j $k $l $m $n $o $r))))))))))))))))))

(= (binding-test:bindings mixed bind-capture) ((Y (f X))))
(= (binding-test:bindings tagged bind-capture) (((var "Y") ((word "f") (var "X")))))
(= (binding-test:bindings mixed bind-shadowed) ((X a)))
(= (binding-test:bindings tagged bind-shadowed) (((var "X") (word "a"))))
(= (binding-test:bindings mixed bind-telescope) ((B (f A))))
(= (binding-test:bindings tagged bind-telescope) (((var "B") ((word "f") (var "A")))))
(= (binding-test:bindings mixed bind-constant) ((Z c)))
(= (binding-test:bindings tagged bind-constant) (((var "Z") (word "c"))))

