; The generated decision program must prune weak-head-disjoint equation
; candidates without changing Prime call-by-need, syntax, or branch effects.

(= (decision-literal a) literal-a)
(= (decision-literal b) literal-b)
(= (decision-literal c) literal-c)
(= (decision-literal d) literal-d)
(= (decision-literal e) literal-e)
(= (decision-literal f) literal-f)
(= (decision-literal g) literal-g)
(= (decision-literal h) literal-h)
(= (decision-literal i) literal-i)
(= (decision-literal j) literal-j)
!(decision-literal c)

(= (decision-head (left $x)) (l $x))
(= (decision-head (right $x)) (r $x))
(= (decision-head (middle $x)) (m $x))
(= (decision-head (north $x)) (n $x))
(= (decision-head (south $x)) (s $x))
(= (decision-head (east $x)) (e $x))
(= (decision-head (west $x)) (w $x))
(= (decision-head (up $x)) (u $x))
(= (decision-head (down $x)) (d $x))
!(decision-head (right 7))

(= (decision-arity (node)) arity-zero)
(= (decision-arity (node $x)) arity-one)
(= (decision-arity (node $x $y)) arity-two)
!(decision-arity (node 1 2))

; An unready callable argument must be demanded once by the decision program,
; not rediscovered by a full scan of every strict clause.
(= (decision-producer) (right 9))
!(decision-head (decision-producer))

; The direct demand must preserve branch-local effect and update-cell identity.
!(bind! &decision-counter (new-state 0))
(= (decision-effect-producer)
   (let $old (get-state &decision-counter)
     (let $_ (change-state! &decision-counter (+ $old 1))
       (right $old))))
!(decision-head (decision-effect-producer))
!(get-state &decision-counter)

; A wildcard clause can make an argument semantically irrelevant.  The
; decision program must fall back to the exact matcher instead of forcing an
; effect merely because other clauses discriminate that position.
!(bind! &decision-wild-counter (new-state 0))
(= (decision-wild A $x) wildcard-a)
(= (decision-wild B b) literal-b)
(= (decision-wild C c) literal-c)
(= (decision-wild D d) literal-d)
(= (decision-wild E e) literal-e)
(= (decision-wild F f) literal-f)
(= (decision-wild G g) literal-g)
(= (decision-wild H h) literal-h)
(= (decision-wild-producer)
   (let $old (get-state &decision-wild-counter)
     (let $_ (change-state! &decision-wild-counter (+ $old 1))
       b)))
!(decision-wild A (decision-wild-producer))
!(get-state &decision-wild-counter)
