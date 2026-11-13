!(import! &self (library lib_transform))
!(import! &self (library lib_petta_by_transform))

; Each law runs a PeTTa form and the same form written with transform, and
; compares their answers as bags.  A law over a store runs each side on its own
; space, filled alike, and compares the stores left as well.
(: law (-> Atom Atom Atom %Undefined%))
(= (law $name $written $by-transform)
   (let $a (sort-atom (collapse (eval $written)))
     (let $b (sort-atom (collapse (eval $by-transform)))
       (if (=alpha $a $b) (holds $name) (FAILS $name $a $b)))))

(= (rows $s) (sort-atom (collapse (match $s $row $row))))
(= (fill $s)
   (let* (($_1 (add-atom $s (p 1))) ($_2 (add-atom $s (p 2))) ($_3 (add-atom $s (p 1)))
          ($_4 (add-atom $s (q 2))) ($_5 (add-atom $s (take 5))) ($_6 (add-atom $s (, x y))))
     filled))
!(bind! &a (new-space))
!(bind! &b (new-space))
!(fill &a)
!(fill &b)

; reads
!(law match-one (match &a (p $x) (tag $x (+ $x 10))) (pt:match &b (p $x) (tag $x (+ $x 10))))
!(law match-join (match &a (, (p $x) (q $x)) (both $x)) (pt:match &b (, (p $x) (q $x)) (both $x)))
!(law match-none (match &a (r $x) $x) (pt:match &b (r $x) $x))
!(law match-source-word (match &a (take $n) (took $n)) (pt:match &b (take $n) (took $n)))
!(law match-anything (match &a $x $x) (pt:match &b $x $x))
!(law get-atoms (get-atoms &a) (pt:get-atoms &b))

; writes
!(law add-answer (add-atom &a (r 5)) (pt:add-atom &b (r 5)))
!(law add-unevaluated (add-atom &a (r (+ 1 2))) (pt:add-atom &b (r (+ 1 2))))
!(law add-comma (add-atom &a (, u v)) (pt:add-atom &b (, u v)))
!(law add-duplicate (add-atom &a (p 1)) (pt:add-atom &b (p 1)))
!(law add-store (rows &a) (rows &b))
!(law remove-one (remove-atom &a (r 5)) (pt:remove-atom &b (r 5)))
!(law remove-none (remove-atom &a (r 55)) (pt:remove-atom &b (r 55)))
!(law remove-occurrences (remove-atom &a (p 1)) (pt:remove-atom &b (p 1)))
!(law remove-by-pattern (remove-atom &a (r $y)) (pt:remove-atom &b (r $y)))
!(law remove-store (rows &a) (rows &b))

; choice
!(law superpose (superpose (a b (+ 1 2))) (pt:superpose (a b (+ 1 2))))
!(law superpose-none (superpose ()) (pt:superpose ()))
!(law empty (empty) (pt:empty))
!(law once (once (superpose (1 2 3))) (pt:once (superpose (1 2 3))))
!(law once-none (once (empty)) (pt:once (empty)))
!(law collapse (collapse (superpose (1 2 (+ 1 2)))) (pt:collapse (superpose (1 2 (+ 1 2)))))
!(law collapse-none (collapse (empty)) (pt:collapse (empty)))
!(law collapse-match (collapse (match &a (p $x) $x)) (pt:collapse (pt:match &b (p $x) $x)))

; binding
!(law let (let ($u $v) (1 2) (got $u $v)) (pt:let ($u $v) (1 2) (got $u $v)))
!(law let-no-match (let (a $u) (b 1) (got $u)) (pt:let (a $u) (b 1) (got $u)))
!(law let-every-answer (let $u (superpose (1 2 3)) (got $u)) (pt:let $u (superpose (1 2 3)) (got $u)))
!(law let-body-runs (let $u 4 (+ $u 1)) (pt:let $u 4 (+ $u 1)))
!(law let-star (let* (($u (+ 1 1)) ($v (* $u 10))) (got $u $v))
               (pt:let* (($u (+ 1 1)) ($v (* $u 10))) (got $u $v)))
!(law let-star-product (let* (($u (superpose (1 2))) ($v (superpose (a b)))) (got $u $v))
                       (pt:let* (($u (superpose (1 2))) ($v (superpose (a b)))) (got $u $v)))

; branching
!(law if-then (if (> 2 1) (yes (+ 1 1)) (no)) (pt:if (> 2 1) (yes (+ 1 1)) (no)))
!(law if-else (if (> 1 2) (yes) (no (+ 1 1))) (pt:if (> 1 2) (yes) (no (+ 1 1))))
!(law if-each-answer (if (superpose (True False True)) yes no)
                     (pt:if (superpose (True False True)) yes no))
!(law when-true (if (> 2 1) yes) (pt:when (> 2 1) yes))
!(law when-false (if (> 1 2) yes) (pt:when (> 1 2) yes))
!(law case-first (case (+ 1 1) ((1 one) (2 two) ($other many))) (pt:case (+ 1 1) ((1 one) (2 two) ($other many))))
!(law case-earlier-wins (case 2 (($n (any $n)) (2 two))) (pt:case 2 (($n (any $n)) (2 two))))
!(law case-binds (case (pair 3 4) (((pair $l $r) (+ $l $r)) ($other none)))
                 (pt:case (pair 3 4) (((pair $l $r) (+ $l $r)) ($other none))))
!(law case-no-row (case 7 ((1 one) (2 two))) (pt:case 7 ((1 one) (2 two))))
!(law case-each-answer (case (superpose (1 2 3)) ((1 one) (2 two) ($other many)))
                       (pt:case (superpose (1 2 3)) ((1 one) (2 two) ($other many))))

; equations
(= (fact 0) 1)
(= (fact $n) (if (> $n 0) (* $n (fact (- $n 1))) (empty)))
(= (color) red)
(= (color) (superpose (green blue)))
(= (swap (pair $l $r)) (pair $r $l))
!(pt:define (= (fact 0) 1))
!(pt:define (= (fact $n) (pt:if (> $n 0) (* $n (pt:call (fact (- $n 1)))) (pt:empty))))
!(pt:define (= (color) red))
!(pt:define (= (color) (pt:superpose (green blue))))
!(pt:define (= (swap (pair $l $r)) (pair $r $l)))
!(law call-base (fact 0) (pt:call (fact 0)))
!(law call-recursive (fact 5) (pt:call (fact 5)))
!(law call-every-equation (color) (pt:call (color)))
!(law call-head-pattern (swap (pair 1 2)) (pt:call (swap (pair 1 2))))
!(law call-no-equation-matches (swap 3) (pt:call (swap 3)))
!(law call-argument-evaluated (fact (+ 1 2)) (pt:call (fact (+ 1 2))))
!(law call-each-argument-answer (fact (superpose (1 2 3))) (pt:call (fact (superpose (1 2 3)))))

; a firing hands the caller a row of bindings: the template is ordinary code
!(bind! &c (new-space))
!(bind! &d (new-space))
!(add-atom &c (foo 1))
!(add-atom &c (expr (+ 1 2)))
!(add-atom &d (foo 1))
!(add-atom &d (expr (+ 1 2)))
(: holding (-> Atom Atom))
(= (holding $x) $x)
!(law read-binds (match &c (foo $x) (tag $x (+ $x 10)))
                 (let True (pt:read &d (quote (foo $x))) (tag $x (+ $x 10))))
!(law read-held-position (match &c (foo $x) (holding (wrap $x)))
                         (let True (pt:read &d (quote (foo $x))) (holding (wrap $x))))
!(law read-quoted-position (match &c (foo $x) (quote (wrap $x)))
                           (let True (pt:read &d (quote (foo $x))) (quote (wrap $x))))
!(law stored-code-read (match &c (expr $e) (kept $e))
                               (let True (pt:read &d (quote (expr $e))) (kept $e)))
!(law read-then-write (match &c (foo $x) (add-atom &c (bar $x)))
                      (let True (pt:read &d (quote (foo $x))) (pt:add-atom &d (bar $x))))
!(law read-then-write-store (rows &c) (rows &d))
!(law stored-code-read-join (match &c (, (foo $x) (expr $e)) (both $x $e))
                (let True (pt:read &d (quote (, (foo $x) (expr $e)))) (both $x $e)))

; A match answers from the store as it is at the call: the code that consumes
; its answers removes a row not answered yet, or adds one, and the answers stay.
!(bind! &e (new-space))
!(bind! &f (new-space))
(= (pt:clear $s $p) (if (remove-atom $s $p) (pt:clear $s $p) ()))
(= (refill $s)
   (let* (($_0 (pt:clear $s (n $any))) ($_1 (add-atom $s (n 1))) ($_2 (add-atom $s (n 2))))
     filled))
(= (drop-two $s $x) (let $_gone (remove-atom $s (n 2)) $x))
(= (add-three $s $x) (let $_made (add-atom $s (n 3)) $x))
!(refill &e)
!(refill &f)
!(law match-answers-a-row-removed-meanwhile
      (drop-two &e (match &e (n $x) $x))
      (drop-two &f (pt:match &f (n $x) $x)))
!(refill &e)
!(refill &f)
!(law read-answers-a-row-removed-meanwhile
      (drop-two &e (match &e (n $x) $x))
      (drop-two &f (let True (pt:read &f (quote (n $x))) $x)))
!(refill &e)
!(refill &f)
!(law match-does-not-answer-a-row-added-meanwhile
      (add-three &e (match &e (n $x) $x))
      (add-three &f (let True (pt:read &f (quote (n $x))) $x)))
!(refill &e)
!(refill &f)
!(law get-atoms-answers-a-row-removed-meanwhile
      (drop-two &e (get-atoms &e))
      (drop-two &f (pt:get-atoms &f)))

; A continuation adds a row between answers. CeTTa's conjunction snapshot
; excludes it, while SWI starts its next leg from the changed store. The
; selected variant reproduces its host. pt:match always takes one snapshot.
!(bind! &g (new-space))
!(bind! &h (new-space))
(= (sides $s)
   (let* (($_0 (pt:clear $s (right $any))) ($_1 (pt:clear $s (left $other)))
          ($_2 (add-atom $s (left a))) ($_3 (add-atom $s (left b))) ($_4 (add-atom $s (right a))))
     filled))
(= (add-right-b $s $x) (let $_made (add-atom $s (right b)) $x))
!(sides &g)
!(sides &h)
!(law conjunction-written-follows-host-variant
      (add-right-b &g (match &g (, (left $x) (right $x)) $x))
      (if (== (pt:variant) swi) (superpose (a b)) a))
!(sides &g)
!(law conjunction-read-follows-host-variant
      (add-right-b &g (match &g (, (left $x) (right $x)) $x))
      (add-right-b &h (let True (pt:read &h (quote (, (left $x) (right $x)))) $x)))
!(sides &h)
!(law conjunction-by-one-transform-is-in-one-state
      (superpose (a))
      (add-right-b &h (pt:match &h (, (left $x) (right $x)) $x)))

; equations that stay compiled: the rule head is read, the equation is called
(= (grow $n) (next $n))
(= (grow zero) one)
(= (keep $e) (kept $e))
(= (first-of (cons $h $t)) $h)
(= (pt:body (pt:key grow test 1) (pt:args $n)) (next $n))
(= (pt:body (pt:key grow test 2) (pt:args zero)) one)
(= (pt:body (pt:key keep test 3) (pt:args $e)) (kept $e))
(= (pt:body (pt:key first-of test 4) (pt:args (cons $h $t))) $h)
!(pt:rule! grow ($n) (pt:key grow test 1))
!(pt:rule! grow (zero) (pt:key grow test 2))
!(pt:rule! keep ($e) (pt:key keep test 3))
!(pt:rule! first-of ((cons $h $t)) (pt:key first-of test 4))
!(law apply-one (grow 4) (pt:apply grow (4)))
!(law apply-every-head (grow zero) (pt:apply grow (zero)))
!(law stored-code-apply (keep (quote (+ 1 2))) (pt:apply keep (quote ((+ 1 2)))))
!(law apply-binds-the-caller (let $u $w (pair $u (grow $w))) (let $u $w (pair $u (pt:apply grow (cons-atom $w ())))))
!(law apply-list-view-in-the-head (first-of (1 2 3)) (pt:apply first-of ((1 2 3))))

; a value the host can apply, held as an argument: the partial application (+ 1)
; and a lambda.  A list of such values is itself an application wherever it is
; evaluated, so rule heads hold their patterns under a data head.
(= (use $f $x) ($f $x))
(= (pt:body (pt:key use test 5) (pt:args $f $x)) ($f $x))
!(pt:rule! use ($f $x) (pt:key use test 5))
!(law apply-partial-application (use (+ 1) 2) (pt:apply use (cons-atom (+ 1) (cons-atom 2 ()))))
!(law apply-lambda (let $l (|-> ($y) (* $y 3)) (use $l 2))
                   (let $l (|-> ($y) (* $y 3)) (pt:apply use (cons-atom $l (cons-atom 2 ())))))
!(law partial-application-in-a-pattern (let $f (+ 1) (match &a (p $x) (seen $f $x)))
                                       (let $f (+ 1) (pt:match &b (p $x) (seen $f $x))))

; a head part that the host runs: the stored rule head holds a variable there
(= (same-as $a $b) (let $a $b $a))
(= (whole (same-as $l (cons $h $t))) (pair $h $l))
(= (pt:body (pt:key whole test 6) (pt:args (same-as $l (cons $h $t)))) (pair $h $l))
!(pt:rule! whole ((same-as $l (cons $h $t))) (pt:key whole test 6))
!(law apply-function-in-the-head (whole (1 2 3)) (pt:apply whole ((1 2 3))))

; a space named by a symbol, never bound as a handle
!(law add-under-a-name (let $_ (add-atom named-a (r 1)) (match named-a (r $x) $x))
                       (let $_ (pt:add-atom named-b (r 1)) (pt:match named-b (r $x) $x)))
; (the made atom is code held as data: on the caller route, like the stored-code laws)
!(law stored-code-add-under-a-name (let $_ (add-atom named-a (r (+ 1 2))) (get-atoms named-a))
                                   (let $_ (pt:add-atom named-b (r (+ 1 2))) (pt:get-atoms named-b)))

; ---- answers in order -----------------------------------------------------------
; The laws above compare answers as bags, which is what a read of a space
; promises.  Where PeTTa promises an order, the answers are compared in order:
; the equations of a function, the elements of a superpose, the rows of a case,
; the first answer once takes, and the effects of evaluating in that order.
(: law-in-order (-> Atom Atom Atom %Undefined%))
(= (law-in-order $name $written $by-transform)
   (let $a (collapse (eval $written))
     (let $b (collapse (eval $by-transform))
       (if (=alpha $a $b) (holds $name) (FAILS $name $a $b)))))

(= (rank) third)
(= (rank) first)
(= (rank) second)
(= (pt:body (pt:key rank test 7) (pt:args)) third)
(= (pt:body (pt:key rank test 8) (pt:args)) first)
(= (pt:body (pt:key rank test 9) (pt:args)) second)
!(pt:rule! rank () (pt:key rank test 7))
!(pt:rule! rank () (pt:key rank test 8))
!(pt:rule! rank () (pt:key rank test 9))
!(law-in-order ordered-equations (rank) (pt:apply rank ()))
!(law-in-order ordered-once-equation (once (rank)) (pt:once (pt:apply rank ())))
!(law-in-order ordered-superpose (superpose (c a b a)) (pt:superpose (c a b a)))
!(law-in-order ordered-once-superpose (once (superpose (c a b))) (pt:once (pt:superpose (c a b))))
!(law-in-order ordered-case-first-row (case (superpose (2 1 2)) ((1 one) (2 two) ($n (late $n))))
                                      (pt:case (pt:superpose (2 1 2)) ((1 one) (2 two) ($n (late $n)))))
!(law-in-order ordered-let-nesting (let $x (superpose (b a)) (let $y (superpose (2 1)) (pair $x $y)))
                                   (pt:let $x (pt:superpose (b a)) (pt:let $y (pt:superpose (2 1)) (pair $x $y))))
!(bind! &ticks-a (new-state 0))
!(bind! &ticks-b (new-state 0))
(= (tick-on $s) (let $v (+ (get-state $s) 1) (let $_ (change-state! $s $v) $v)))
!(law-in-order ordered-effects (let $r (rank) (stamp $r (tick-on &ticks-a)))
                               (let $r (pt:apply rank ()) (stamp $r (tick-on &ticks-b))))

; Negative example.  once written with one transform answers what once answers,
; and it runs every alternative first: a transform's candidates are enumerated
; before the first of them fires.  Under the host's once the later alternatives
; never run.  The two counts are pinned, so a change of either is noticed.
!(bind! &once-a (new-state 0))
!(bind! &once-b (new-state 0))
!(law-in-order once-written-runs-one-alternative
     (let $_ (once (let $r (rank) (tick-on &once-a))) (get-state &once-a)) 1)
!(law-in-order once-by-transform-runs-every-alternative
     (let $_ (pt:once (let $r (pt:apply rank ()) (tick-on &once-b))) (get-state &once-b)) 3)

; Negative examples.  A form written with one transform holds its expression
; and its body as data.  Two things follow, and both are pinned here.
;   A value inside a held expression is evaluated again: the host hands
;   (same-value $x) the value of $x as it is; the transform evaluates the
;   expression (same-value (+ 1 2)) from the start.
;   A pattern is data: the host evaluates the let pattern (cons $h $t) to a list
;   read by head and tail; the transform matches the expression (cons $h $t).
; For whole programs, binding is therefore left to the host's unifier, which is
; the solver of the told equation (lib_step_rules.metta states a tell that way).
(= (same-value $v) $v)
!(law-in-order let-written-hands-a-value-over
     (repr (collapse (let $x (quote (+ 1 2)) (let $y (same-value $x) (got $y))))) "((got (+ 1 2)))")
!(law-in-order let-by-transform-evaluates-a-held-value-again
     (repr (collapse (let $x (quote (+ 1 2)) (pt:let $y (same-value $x) (got $y))))) "((got 3))")
!(law-in-order let-written-reads-a-list-by-head-and-tail
     (collapse (let (cons $h $t) (1 2 3) (pair $h $t))) ((pair 1 (2 3))))
!(law-in-order let-by-transform-takes-the-pattern-as-data
     (collapse (pt:let (cons $h $t) (1 2 3) (pair $h $t))) ())

; ---- a kernel let that chooses ----------------------------------------------------
; Each answer of a kernel let is a match of the kernel, so each is a branch of its
; own, and a branch fires the answer it stands for.
!(law-in-order kernel-let-choice-every-answer
     (superpose (1 2 3)) (transform &b (let $x (superpose (1 2 3))) $x))
!(law kernel-let-choice-with-a-read
     (let $k (match &a (q $k) $k) (let $x (superpose (u v)) (pair $k $x)))
     (transform &b (, (q $k) (let $x (superpose (u v)))) (pair $k $x)))
!(law-in-order kernel-let-choice-keeps-copies
     (superpose (1 1 2)) (transform &b (let $x (superpose (1 1 2))) $x))
; Pinned: the expression of a kernel let runs when the transform is called, to
; propose, and again when a branch fires, so an effect in it happens twice for
; one branch.
!(bind! &let-ticks (new-state 0))
!(law-in-order kernel-let-runs-at-the-call-and-at-the-firing
     (let $_ (collapse (transform &b (let $v (tick-on &let-ticks)) (got $v))) (get-state &let-ticks)) 2)

; ---- case with an Empty row -------------------------------------------------------
!(law case-empty-row-taken (case (empty) ((1 one) (Empty none))) (pt:case (empty) ((1 one) (Empty none))))
!(law case-empty-row-not-taken (case (superpose (1 2)) ((1 one) (Empty none)))
                               (pt:case (superpose (1 2)) ((1 one) (Empty none))))
!(law case-empty-row-and-no-match (case (superpose (5 6)) ((1 one) (Empty none)))
                                  (pt:case (superpose (5 6)) ((1 one) (Empty none))))
!(law case-empty-row-binds (case (superpose (3 4)) ((Empty none) ($n (seen $n))))
                           (pt:case (superpose (3 4)) ((Empty none) ($n (seen $n)))))

; ---- a mark, stored or not --------------------------------------------------------
; A mark that is not stored is seen as absent, once; a stored one is not.
!(law mark-not-stored-is-absent (superpose (pt:absent)) (let $m (pt:fresh) (pt:unmarked $m)))
!(law mark-stored-is-not-absent (empty)
                                (let $m (pt:fresh) (let $_ (pt:cut! $m) (pt:unmarked $m))))

; Internal source words are still legal heads of stored data.
!(bind! &source-names (new-space))
!(add-atom &source-names (tf:let-first a b c))
!(law match-guided-let-source-word
      (match &source-names (tf:let-first $a $b $c) (fields $a $b $c))
      (pt:match &source-names (tf:let-first $a $b $c) (fields $a $b $c)))
