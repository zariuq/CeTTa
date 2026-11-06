; A typed relation's calls to itself skip their protocol once a checked
; call's arguments meet the entry facts; every answer below is the one the
; complete protocol gives.

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

; A chainer-shaped relation: a knowledge space, a ground depth, and a
; polymorphic query whose instance is the result.
(: walk (-> $kb Nat $q $q))
(= (walk $kb $_ (: $p $t)) (match $kb (: $p $t) (: $p $t)))
(= (walk $kb (S $n) (: ($r $a) $t))
   (let* (((: $r (-> (: $a $u) $t)) (walk $kb $n (: $r (-> (: $a $u) $t))))
          ((: $a $u) (walk $kb $n (: $a $u))))
     (: ($r $a) $t)))

; Uninterpreted knowledge: the version runs.
!(bind! &kb1 (new-space))
!(add-atom &kb1 (: ax A))
!(add-atom &kb1 (: rule (-> (: $x A) B)))
!(collapse (walk &kb1 (S (S Z)) (: $prf B)))

; Knowledge naming a declared symbol: its rows are typed through Z's
; declaration, so the entry is refused.
!(bind! &kb2 (new-space))
!(add-atom &kb2 (: ax Z))
!(add-atom &kb2 (: rule (-> (: $x Z) B)))
!(collapse (walk &kb2 (S (S Z)) (: $prf B)))

; A query naming a declared symbol.
!(collapse (walk &kb1 (S (S Z)) (: $prf S)))

; A depth that is not ground, sharing a variable with the query: the query
; binds it to a proof term, which the depth guard then refuses.
!(collapse (walk &kb1 (S $d) (: $d B)))

; A rule named by a function: the result's application would run it.
(= (twice $x) ($x $x))
!(bind! &kb3 (new-space))
!(add-atom &kb3 (: ax A))
!(add-atom &kb3 (: twice (-> (: $x A) B)))
!(collapse (walk &kb3 (S (S Z)) (: $prf B)))

; Knowledge changed between runs is judged again.
!(add-atom &kb1 (: lift (-> (: $x B) Z)))
!(collapse (walk &kb1 (S (S (S Z))) (: $prf Z)))
!(collapse (walk &kb1 (S (S Z)) (: $prf B)))

; Duplicate answers stay distinct occurrences.
!(bind! &kb4 (new-space))
!(add-atom &kb4 (: ax A))
!(add-atom &kb4 (: ax A))
!(add-atom &kb4 (: rule (-> (: $x A) B)))
!(collapse (walk &kb4 (S (S Z)) (: $prf B)))

; A premise proved by an application of a declared function: the inner
; result guard rejects (S Z) for the application-shaped premise ($g $y),
; so the complete protocol has no answer here.
!(bind! &kb5 (new-space))
!(add-atom &kb5 (: rule (-> (: ($g $y) A) B)))
!(add-atom &kb5 (: (S Z) A))
!(collapse (walk &kb5 (S (S Z)) (: $prf B)))

; Parameterized constructors.  A requirement such as (List Number) fixes the
; type variables of a constructor's one declaration, so a pattern's fields
; take their instances and a cell built from typed parts takes its type: the
; calls a fold makes to itself skip their protocol.
(: List (-> Type Type))
(: Nil (List $a))
(: Cons (-> $a (List $a) (List $a)))
(: lbuild (-> Number (List Number) (List Number)))
(= (lbuild $n $acc) (if (== $n 0) $acc (lbuild (- $n 1) (Cons $n $acc))))
(: lsum (-> (List Number) Number))
(= (lsum Nil) 0)
(= (lsum (Cons $x $xs)) (+ $x (lsum $xs)))
!(lsum (lbuild 30 Nil))

; An element the requirement refuses: the call has no answer.
!(collapse (lsum (Cons 1 (Cons "two" Nil))))

; Nested instances, and a call into another relation's version.
(: rows (-> (List (List Number)) Number))
(= (rows Nil) 0)
(= (rows (Cons $row $rest)) (+ (lsum $row) (rows $rest)))
!(rows (Cons (Cons 1 (Cons 2 Nil)) (Cons (Cons 3 Nil) Nil)))
!(collapse (rows (Cons (Cons 1 Nil) (Cons 5 Nil))))

; Two type parameters.
(: Pair (-> Type Type Type))
(: MkPair (-> $a $b (Pair $a $b)))
(: firsts (-> (List (Pair Number String)) Number))
(= (firsts Nil) 0)
(= (firsts (Cons (MkPair $n $s) $rest)) (+ $n (firsts $rest)))
!(firsts (Cons (MkPair 1 "a") (Cons (MkPair 2 "b") Nil)))
!(collapse (firsts (Cons (MkPair 1 2) Nil)))

; A guard at a type get-metatype answers, Expression here, passes a value
; get-type has no answer for, so it inverts no pattern through a
; declaration and types no construction: the protocol's answers stand.
(: CMeta (-> Number Expression))
(: gmeta (-> Number Number))
(= (gmeta $n) 42)
(: fmeta (-> Expression Number))
(= (fmeta (CMeta $s)) (gmeta $s))
!(collapse (fmeta (CMeta "x")))
!(collapse (fmeta (CMeta 1)))
(: WrapMeta (-> Type Type))
(: BoxMeta (-> $a (WrapMeta $a)))
(: hmeta (-> (WrapMeta Expression) Number))
(= (hmeta $w) 7)
(: kmeta (-> Expression Number))
(= (kmeta $e) (hmeta (BoxMeta $e)))
!(collapse (kmeta (foo bar)))

; A variable two fields name and the result does not couples the fields,
; so the declaration gives neither a type.
(: Prop Type)
(: Same (-> $a $a Prop))
(: both (-> Prop Number))
(= (both (Same $x $y)) 1)
!(both (Same 1 2))
!(collapse (both (Same 1 "x")))

; get-type never checks a field declared %Undefined% or `_`: such a field
; may hold any value, gives its pattern variable no type, and asks nothing
; of a construction.
(: UBoxT Type)
(: UBox (-> %Undefined% UBoxT))
(: WBoxT Type)
(: WBox (-> _ WBoxT))
(: ucount (-> UBoxT Number Number))
(= (ucount (UBox $x) $n) (if (== $n 0) 0 (+ 1 (ucount (UBox $x) (- $n 1)))))
!(ucount (UBox "q") 5)
!(ucount (UBox (foo bar)) 3)
(: needs-number (-> Number Number))
(= (needs-number $n) $n)
(: ufirst (-> UBoxT Number))
(= (ufirst (UBox $x)) (needs-number $x))
!(collapse (ufirst (UBox 4)))
!(collapse (ufirst (UBox "s")))
(: wcount (-> WBoxT Number Number))
(= (wcount (WBox $x) $n) (if (== $n 0) 0 (+ 1 (wcount (WBox $x) (- $n 1)))))
!(wcount (WBox "w") 4)

; A function passed as a constant takes its closed declared arrow as its
; type; one declared at another arrow does not meet the formal.
(: incN (-> Number Number))
(= (incN $x) (+ $x 1))
(: dblS (-> String String))
(= (dblS $s) $s)
(: applyN (-> Number (-> Number Number) Number Number))
(= (applyN $n $f $acc) (if (== $n 0) $acc (applyN (- $n 1) $f ($f $acc))))
!(applyN 20 incN 0)
!(collapse (applyN 3 dblS 0))
(: PairT Type)
(: P0 (PairT))
(: pfirst (-> (PairT) Number Number))
(= (pfirst $p $n) (if (== $n 0) 1 (pfirst P0 (- $n 1))))
!(pfirst P0 4)

; A version's polymorphic pair holds of a ground argument only.  Here the
; ANY argument shares the first argument's open head: binding it to the
; declared Known turns ($x 1) into (Known 1), typed Tag, which the result
; guard rejects, so launch has no answer.  An undeclared head keeps the
; argument uninterpreted, which the guard accepts; a ground argument, or an
; open one sharing no variable, keeps its type, and a ground one enters
; the version.
(: Known (-> Number Tag))
(: alias-bind-u (-> $t %Undefined% $t))
(= (alias-bind-u $a $b) (let $b Known $a))
(: launch (-> Number Number))
(= (launch $ignored) (let $result (alias-bind-u ($x 1) $x) 1))
!(collapse (launch 0))
(: clean-bind-u (-> $t %Undefined% $t))
(= (clean-bind-u $a $b) (let $b plainhead $a))
(: launch-clean (-> Number Number))
(= (launch-clean $ignored) (let $result (clean-bind-u ($x 1) $x) 1))
!(collapse (launch-clean 0))
(: launch-ground (-> Number Number))
(= (launch-ground $ignored) (let $result (alias-bind-u (plainbox 1) $y) 1))
!(collapse (launch-ground 0))
(: launch-apart (-> Number Number))
(= (launch-apart $ignored) (let $result (alias-bind-u ($x 1) $z) 1))
!(collapse (launch-apart 0))
!(collapse (alias-bind-u (plainbox 1) $w))
!(collapse (alias-bind-u ($v 1) $v))
!(collapse (clean-bind-u ($v 1) $v))

; An open polymorphic argument stays out of the version even where every
; other argument is ground: its variables reach names that carry no claim.
; A head pattern shares the open head with a ground argument, an untyped
; call's answer carries the open argument, a callee's head pattern binds
; it; with one parameter there is no other argument at all.  Each binds the
; open head to Known, and the result guard rejects the changed argument.
(= (bind-head ($k $n) $k) ($k $n))
(: bind-head (-> $t %Undefined% $t))
(: launch-bh (-> Number Number))
(= (launch-bh $ignored) (let $result (bind-head ($h 1) Known) 1))
!(collapse (launch-bh 0))
!(collapse (bind-head ($h 1) plainhead))
(= (g $x) $x)
(: via-call (-> $t %Undefined% $t))
(= (via-call $a $b) (let ($k 1) (g $a) (let $k Known $a)))
(: launch-vc (-> Number Number))
(= (launch-vc $ignored) (let $result (via-call ($h 1) 0) 1))
!(collapse (launch-vc 0))
(= (g2 ($k $n)) (let $k Known 1))
(: via-callee (-> $t %Undefined% $t))
(= (via-callee $a $b) (let $r (g2 $a) $a))
(: launch-vce (-> Number Number))
(= (launch-vce $ignored) (let $result (via-callee ($h 1) 0) 1))
!(collapse (launch-vce 0))
(: via-call1 (-> $t $t))
(= (via-call1 $a) (let ($k 1) (g $a) (let $k Known $a)))
(: launch-c1 (-> Number Number))
(= (launch-c1 $ignored) (let $result (via-call1 ($h 1)) 1))
!(collapse (launch-c1 0))
(: via-callee1 (-> $t $t))
(= (via-callee1 $a) (let $r (g2 $a) $a))
(: launch-e1 (-> Number Number))
(= (launch-e1 $ignored) (let $result (via-callee1 ($h 1)) 1))
!(collapse (launch-e1 0))
(: via-branch (-> $t Number $t))
(= (via-branch $a $b) (let $r (if (== $b 0) (let $z Known 1) 2) (let ($z 1) $a $a)))
(: launch-vb (-> Number Number))
(= (launch-vb $ignored) (let $result (via-branch ($h 1) 0) 1))
!(collapse (launch-vb 0))
