; Closed deterministic constructor recursion must use the generated pure-call
; continuation rather than the native C stack.
(= (prepared-pack Z $n) (S $n))
(= (prepared-pack (S $m) Z) (prepared-pack $m (S Z)))
(= (prepared-pack (S $m) (S $n))
   (prepared-pack $m (prepared-pack (S $m) $n)))
(= (prepared-p2i Z) 0)
(= (prepared-p2i (S $x)) (+ 1 (prepared-p2i $x)))
(= (prepared-i2p $n)
   (if (== $n 0) Z (S (prepared-i2p (- $n 1)))))
!(prepared-p2i (prepared-pack (prepared-i2p 2) (prepared-i2p 300)))

; Overlap is not determinate.  The prepared machine must decline and preserve
; the active language's canonical bag/retry behavior.
(= (prepared-choice $x) left)
(= (prepared-choice $x) right)
!(prepared-choice value)

; A later alternative can itself continue through the same head.  Prime's
; explicit driver must not park that continuation on a native-stack
; OutcomeSet, and conservative decline must preserve both occurrences.
(= (prepared-deferred-choice (Cons $x $xs)) $x)
(= (prepared-deferred-choice (Cons $x (Cons $y $ys)))
   (prepared-deferred-choice (Cons $y $ys)))
!(prepared-deferred-choice (Cons 1 (Cons 2 Nil)))

; The generated bind instruction must implement the canonical structural-let
; contract: fresh fields bind positionally, repeated fields co-refer, and a
; variable already visible from the clause head is a rigid reference.
(= (prepared-unpack $pair)
   (let (Pair $x $y) $pair (Unpacked $x $y)))
!(prepared-unpack (Pair a b))

(= (prepared-same $pair)
   (let (Pair $x $x) $pair (Same $x)))
!(prepared-same (Pair same same))
; Negative: unequal repeated fields must not execute the body.
!(prepared-same (Pair left right))

(= (prepared-rigid $x $pair)
   (let (Pair $x $y) $pair (Rigid $x $y)))
!(prepared-rigid same (Pair same tail))
; Negative: a binder occurrence referring to the outer $x must not shadow it.
!(prepared-rigid same (Pair other tail))

; Eager compositions apply the canonical argument-congruence rule beneath an
; inert constructor.  This pins the distinction between constructor data and
; MeTTa quote: a nested callable must not leak into the final value.
(= (prepared-nested-leaf $x) (+ $x 1))
(= (prepared-nested-constructor $x)
   (Box (prepared-nested-leaf $x)))
!(prepared-nested-constructor 41)

; Eager call arguments and clause bindings are values after their producing
; child completes.  A flat expression value must remain data across recursive
; tail calls rather than being reinterpreted as a dynamic operator call.
(= (prepared-flat-member $needle $items)
   (if (== $items ())
       False
       (let ($head $tail) (decons-atom $items)
         (if (== $needle $head)
             True
             (prepared-flat-member $needle $tail)))))
!(prepared-flat-member 2 (1 2 3))
; Negative: exhausting the same expression-valued tail must remain false.
!(prepared-flat-member 4 (1 2 3))

; A grounded data head does not make an expression callable.  Its children
; are still eager computations and must be rebuilt from their values.
(= (prepared-grounded-data $x) (1 (+ $x 3)))
!(prepared-grounded-data 2)

; Negative: an expression-valued head remains dynamically callable.  The
; prepared fragment must decline rather than reclassifying it as data.
(= (prepared-dynamic-head) +)
!((prepared-dynamic-head) 20 22)

; Intrinsic admission is structural rather than guest-specific: a generated
; strict primitive and a successful expression decomposition compose through
; the same bind program.  The pattern is syntax and must not be scanned as an
; executable variable-headed expression.
(= (prepared-intrinsic-composition $ignored)
   (let ($head $tail) (decons-atom (a b c))
     (Triple $head (abs-math -7) $tail)))
!(prepared-intrinsic-composition ignored)

; The free-monoid list algebra is one generated intrinsic language.  This
; exercises constructors, observers, stable sorting, filtering, duplicate
; removal, and order-statistic retention without interpreter recursion.
(= (prepared-list-algebra $ignored)
   (prepared-list-joined (union-atom (3 1) (2 1 4))))
(= (prepared-list-joined $items)
   (prepared-list-sorted (sort-numbers-atom $items)))
(= (prepared-list-sorted $items)
   (prepared-list-filtered (remove-all-atom 1 $items)))
(= (prepared-list-filtered $items)
   (prepared-list-unique (unique-atom $items)))
(= (prepared-list-unique $items)
   (ListAlgebra
     (cons-atom 0 $items)
     (size-atom $items)
     (index-atom $items 1)
     (retain-top-k-keyed-atom
       ((0 low) (7 high-a) (7 high-b) (3 middle)) 2)
     (/ 5 2)))
!(prepared-list-algebra ignored)

; Negative: the generated deconstruction instruction implements only the
; successful nonempty-expression arm.  Computation zero and dialect-specific
; errors must decline and replay through the canonical evaluator.
(= (prepared-intrinsic-fallback $ignored) (decons-atom ()))
!(prepared-intrinsic-fallback ignored)
