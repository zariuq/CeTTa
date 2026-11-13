!(import! &self (library lib_transform))
!(import! &self (library petta_machine))

; Each check runs an expression on the host and on the machine, whose every step
; is a transform rule, and compares the answers in order.  An error raised on
; the machine's side is that check's answer, and the other checks go on.
(: check (-> Atom Atom %Undefined%))
(= (check $name $expression)
   (let $host (collapse (eval $expression))
     (let $machine (catch (pm:answers $expression))
       (let $_shown (println! (machine $name $machine))
         (if (=alpha $host $machine) (holds $name (pm:steps)) (FAILS $name $host $machine))))))

; the program, for the host and, as clause rows, for the machine
(= (double $x) (* $x 2))
(= (fib $n) (if (< $n 2) $n (+ (fib (- $n 1)) (fib (- $n 2)))))
(= (colour) red)
(= (colour) green)
(= (colour) blue)
(= (len Nil) 0)
(= (len (Cons $h $t)) (+ 1 (len $t)))
(= (kind (leaf $v)) leaf)
(= (kind (node $l $r)) node)
(= (kind $other) something)
(= (pick $x) (if (> $x 1) $x (empty)))
(= (sort-of $x) (case $x ((1 one) (2 two) ($other (many $other)))))
(= (first-colour) (once (colour)))
(= (all-colours) (collapse (colour)))
(= (sign $x) (progn (if (> $x 0) (cut) (empty)) positive))
(= (sign $x) other)
(= (keep-first) (progn (colour) (cut) kept))
(= (keep-first) never)
(: keep (-> Atom %Undefined%))
(= (keep $code) (kept $code))
(: as-written (-> Number Atom))
(= (as-written $x) (+ 2 $x))
(= (add3 $a $b $c) (+ $a (+ $b $c)))
(= (adder) (+))
(= (merge $x $y) (+ $x $y))
(= (first-of (cons $h $t)) $h)
(= (rest-of (cons $h $t)) $t)
(= (sum-list ()) 0)
(= (sum-list (cons $h $t)) (+ $h (sum-list $t)))
(= (second-of (cons $a (cons $b $more))) $b)
!(pm:program ((= (first-of (cons $h $t)) $h)
              (= (rest-of (cons $h $t)) $t)
              (= (sum-list ()) 0)
              (= (sum-list (cons $h $t)) (+ $h (sum-list $t)))
              (= (second-of (cons $a (cons $b $more))) $b)))
!(pm:type! (: keep (-> Atom %Undefined%)))
!(pm:type! (: as-written (-> Number Atom)))
!(pm:program ((= (keep $code) (kept $code))
              (= (as-written $x) (+ 2 $x))
              (= (add3 $a $b $c) (+ $a (+ $b $c)))
              (= (adder) (+))
              (= (merge $x $y) (+ $x $y))))
!(pm:program ((= (double $x) (* $x 2))
              (= (fib $n) (if (< $n 2) $n (+ (fib (- $n 1)) (fib (- $n 2)))))
              (= (colour) red)
              (= (colour) green)
              (= (colour) blue)
              (= (len Nil) 0)
              (= (len (Cons $h $t)) (+ 1 (len $t)))
              (= (kind (leaf $v)) leaf)
              (= (kind (node $l $r)) node)
              (= (kind $other) something)
              (= (pick $x) (if (> $x 1) $x (empty)))
              (= (sort-of $x) (case $x ((1 one) (2 two) ($other (many $other)))))
              (= (first-colour) (once (colour)))
              (= (all-colours) (collapse (colour)))
              (= (sign $x) (progn (if (> $x 0) (cut) (empty)) positive))
              (= (sign $x) other)
              (= (keep-first) (progn (colour) (cut) kept))
              (= (keep-first) never)))

!(check literal 7)
!(check arithmetic (+ 1 (* 2 3)))
!(check call (double 21))
!(check recursion (fib 7))
!(check clauses-in-order (colour))
!(check if-then (if (< 1 2) yes no))
!(check if-else (if (> 1 2) yes no))
!(check let-binds (let $x (double 2) (+ $x 1)))
!(check let-pattern (let (pair $a $b) (pair 1 2) (+ $a $b)))
!(check let-no-match (let (pair $a $b) (triple 1 2 3) $a))
!(check choice (superpose (1 2 3)))
!(check choice-then-call (let $x (superpose (1 2 3)) (double $x)))
!(check choice-product (let $x (superpose (1 2)) (let $y (superpose (a b)) (pair $x $y))))
!(check empty-drops (let $x (superpose (1 2 3)) (pick $x)))
!(check head-patterns (len (Cons a (Cons b (Cons c Nil)))))
!(check every-matching-clause (kind (node (leaf 1) (leaf 2))))
!(check data-with-calls (wrap (double 2) (colour)))
!(check call-with-choice (double (superpose (1 2))))
!(check no-clause-matches (len 5))

; a space: requests are performed between rules
!(bind! &facts (new-space))
!(add-atom &facts (edge a b))
!(add-atom &facts (edge b c))
!(check match-reads (match &facts (edge $x $y) (step $x $y)))

; an effect runs once on each side, so each side has a space of its own
(: check-apart (-> Atom Atom Atom %Undefined%))
(= (check-apart $name $on-the-host $on-the-machine)
   (let $host (collapse (eval $on-the-host))
     (let $machine (catch (pm:answers $on-the-machine))
       (let $_shown (println! (machine $name $machine))
         (if (=alpha $host $machine) (holds $name (pm:steps)) (FAILS $name $host $machine))))))
!(bind! &h (new-space))
!(bind! &m (new-space))
!(check-apart add-then-match
    (let $u (add-atom &h (edge c d)) (match &h (edge c $y) $y))
    (let $u (add-atom &m (edge c d)) (match &m (edge c $y) $y)))
!(check-apart effects-are-not-undone
    (let $x (superpose (1 2)) (let $u (add-atom &h (seen $x)) (match &h (seen $y) (pair $x $y))))
    (let $x (superpose (1 2)) (let $u (add-atom &m (seen $x)) (match &m (seen $y) (pair $x $y)))))
!(check-apart remove-then-match
    (let $u (remove-atom &h (seen 1)) (match &h (seen $y) $y))
    (let $u (remove-atom &m (seen 1)) (match &m (seen $y) $y)))

; once: the first answer; the choice points made for it are dropped
!(check once-first (once (superpose (1 2 3))))
!(check once-of-a-call (first-colour))
!(check once-none (once (empty)))
!(check once-in-each-branch (let $x (superpose (a b)) (pair $x (once (colour)))))
; the alternatives once drops never run: their effects are not there
!(check-apart once-runs-one-alternative
    (let $first (once (let $x (superpose (1 2 3)) (let $u (add-atom &h (tried $x)) $x)))
      (collapse (match &h (tried $y) $y)))
    (let $first (once (let $x (superpose (1 2 3)) (let $u (add-atom &m (tried $x)) $x)))
      (collapse (match &m (tried $y) $y))))

; collapse: every answer, in order, as one list; it outlives backtracking
!(check collapse-all (collapse (superpose (1 2 3))))
!(check collapse-none (collapse (empty)))
!(check collapse-of-a-call (all-colours))
!(check collapse-nested (collapse (let $x (superpose (1 2)) (collapse (superpose ($x $x))))))
!(check collapse-in-each-branch (let $x (superpose (1 2)) (pair $x (collapse (colour)))))
!(check once-inside-collapse (collapse (once (colour))))

; case: the first row whose pattern meets the value
!(check case-first (sort-of 1))
!(check case-second (sort-of 2))
!(check case-default (sort-of 9))
!(check case-no-row (case 5 ((1 one) (2 two))))
!(check case-each-answer (collapse (case (superpose (1 2 3)) ((2 two) ($x (other $x))))))
!(check case-binds (case (pair 3 4) (((pair $a $a) same) ((pair $a $b) (differ $a $b)))))

; a case with an Empty row: the row is taken only when the key has no value;
; the key's values are met one at a time
!(check case-empty-row-taken (collapse (case (empty) ((1 one) (Empty none)))))
!(check case-empty-row-not-taken (collapse (case (superpose (1 2 3)) ((2 two) (Empty none)))))
!(check case-empty-row-and-no-match (collapse (case (superpose (5 6)) ((1 one) (Empty none)))))
!(check case-empty-row-in-each-branch
    (collapse (let $y (superpose (1 2 1)) (case (if (== $y 1) 5 (empty)) ((5 five) (Empty none))))))
!(check-apart case-with-an-empty-row-streams
    (let $first (once (case (let $x (superpose (5 6 7)) (let $u (add-atom &h (met $x)) $x))
                        ((6 six) (Empty none))))
      (pair $first (collapse (match &h (met $y) $y))))
    (let $first (once (case (let $x (superpose (5 6 7)) (let $u (add-atom &m (met $x)) $x))
                        ((6 six) (Empty none))))
      (pair $first (collapse (match &m (met $y) $y)))))

; progn and cut: a cut drops the later clauses and the choice points of its body
!(check sequence (progn (+ 1 1) (+ 2 2)))
!(check cut-commits-the-clause (collapse (sign 3)))
!(check no-cut-tries-the-next (collapse (sign -3)))
!(check cut-drops-the-body-choices (collapse (keep-first)))

; let*, chain, an if without an else, quote
!(check let-star (let* (($a 1) ($b (+ $a 1))) (pair $a $b)))
!(check let-star-with-patterns (let* (((pair $a $b) (pair 1 2)) ($c (double $b))) (triple $a $b $c)))
!(check chain-binds (chain (double 2) $x (+ $x 1)))
!(check if-without-else-taken (collapse (if (< 1 2) yes)))
!(check if-without-else-not-taken (collapse (if (> 1 2) yes)))
!(check quote-holds (quote (double 2)))

; eval: the value of its argument is code
!(bind! &code (new-space))
!(add-atom &code (code (double 4)))
!(check eval-runs-stored-code (match &code (code $c) (eval $c)))
; eval of an argument that is not written as a variable is that argument,
; evaluated once: a quote answers its code as written and the code is not run.
(= (eval-made) (made 1))
(= (eval-held) (quote (eval-made)))
(= (eval-same $x) $x)
!(pm:program ((= (eval-made) (made 1))
              (= (eval-held) (quote (eval-made)))
              (= (eval-same $x) $x)))
!(check eval-of-a-written-quote-is-the-code-as-written (eval (quote (eval-made))))
!(check eval-of-a-variable-runs-its-value (let $c (quote (eval-made)) (eval $c)))
!(check eval-of-a-written-call-is-the-call-s-answer (eval (eval-held)))
!(check eval-of-a-call-s-answer-in-a-variable-runs-it (let $c (eval-held) (eval $c)))
!(check eval-of-a-quote-passed-through-a-call (eval (eval-same (quote (eval-made)))))
!(check eval-twice-of-a-written-quote (eval (eval (quote (eval-made)))))

; superpose and hyperpose of a list that is computed
!(check superpose-of-a-written-list-headed-by-a-name (superpose (collapse (colour))))
!(check superpose-of-a-variable (let $list (collapse (colour)) (superpose $list)))
!(check hyperpose-is-choice (msort (collapse (hyperpose (3 1 2)))))

; the stream forms
!(check unique-stream (collapse (unique (superpose (a b a c)))))
!(check union-stream (collapse (union (superpose (a b)) (superpose (b c)))))
!(check intersection-stream (collapse (intersection (superpose (a b c)) (superpose (b c d)))))
!(check subtraction-stream (collapse (subtraction (superpose (a b c)) (superpose (b)))))

; operations of the host: one value computed by a rule; several answers, or an
; effect, by a request
!(check host-operation-on-values (cons-atom (double 2) (b c)))
!(check host-operation-with-several-answers (get-atoms &facts))
!(bind! &cell (new-state 1))
!(check host-state (let $_changed (change-state! &cell (double 4)) (get-state &cell)))
!(check data-comes-back (wrapped (double 2) (colour)))

; applied values: a name of the program, an operation of the host, a partial
; application, and a list whose head is a variable
!(check applied-name (let $f double ($f 4)))
!(check applied-host-operation (let $f + ($f 4 5)))
!(check applied-partial (let $f (+ 1) ($f 4)))
!(check applied-with-choice (let $f (superpose (double pick)) ($f 3)))
!(check list-with-a-variable-head (let $x 1 ($x 2)))
!(check applied-expression-head ((+ 1) 2))

; declarations: an argument held as written, a body answered as written
!(check held-argument (keep (double 2)))
!(check body-answered-as-written (as-written 40))

; a name of the program applied in part, and with more arguments than it takes
!(check partial-then-applied (let $f (add3 1 2) ($f 3)))
!(check partial-of-a-partial (let $f (add3 1) (let $g ($f 2) ($g 3))))
!(check over-application (adder 1 2))

; foldall: the answers of its generator, folded
!(check foldall-with-a-host-operation (foldall + (superpose (1 2 3)) 0))
!(check foldall-with-a-name-of-the-program (foldall merge (superpose (1 2 3)) 0))

; an equation added while the program runs is a clause row for what is asked
; next, and removed, it is gone
!(check-apart define-at-run-time
    (add-atom &self (= (late-on-host $x) (* $x 3)))
    (add-atom &self (= (late-on-machine $x) (* $x 3))))
!(check call-defined-at-run-time (late-on-machine 2))
!(check-apart remove-at-run-time
    (remove-atom &self (= (late-on-host $x) $b))
    (remove-atom &self (= (late-on-machine $x) $b)))
!(check call-after-removal (late-on-machine 2))

; PeTTa's list view in the patterns of heads, lets and case rows; cons as an operation
!(check list-view-first (first-of (1 2 3)))
!(check list-view-others (rest-of (1 2 3)))
!(check list-view-recursion (sum-list (1 2 3 4)))
!(check list-view-nested (second-of (a b c)))
!(check list-view-of-no-list (collapse (first-of 5)))
!(check list-view-of-the-empty-list (collapse (first-of ())))
!(check list-view-in-a-let (let (cons $h $t) (a b c) (pair $h $t)))
!(check list-view-in-a-case-row (case (a b c) (((cons $h $t) (pair $h $t)) ($other none))))
!(check cons-builds-a-list (cons 1 (2 3)))

; lambdas: applied on the machine, each application with parameters of its own.
; The host names every lambda it meets in a form it compiles, held code
; included, so the machine is given these expressions as text.
(= (check-text $name $host $text)
   (let $expression (parse $text)
     (let $machine (catch (pm:answers $expression))
       (let $_shown (println! (machine $name $machine))
         (if (=alpha $host $machine) (holds $name (pm:steps)) (FAILS $name $host $machine))))))
!(check-text lambda-applied
    (collapse ((|-> ($x) (* $x 10)) 2))
    "((|-> ($x) (* $x 10)) 2)")
!(check-text lambda-in-a-variable
    (collapse (let $f (|-> ($x $y) (pair $y $x)) ($f 1 2)))
    "(let $f (|-> ($x $y) (pair $y $x)) ($f 1 2))")
!(check-text lambda-applied-twice
    (collapse (let $f (|-> ($x) (double $x)) (pair ($f 1) ($f 2))))
    "(let $f (|-> ($x) (double $x)) (pair ($f 1) ($f 2)))")
!(check-text lambda-keeps-a-value-of-its-surroundings
    (collapse (let $k 5 (let $f (|-> ($x) (+ $x $k)) ($f 1))))
    "(let $k 5 (let $f (|-> ($x) (+ $x $k)) ($f 1)))")

; foldall, maplist: the operation is applied on the machine; prog1
!(check maplist-with-a-name (maplist double (1 2 3)))
!(check foldall-gives-the-item-first (foldall pair (superpose (1 2 3)) 0))
!(check prog1-answers-the-first (prog1 (double 1) (double 2) (double 3)))
!(check-text foldall-with-a-lambda
    (collapse (foldall (|-> ($a $b) (+ $a $b)) (superpose (1 2 3)) 0))
    "(foldall (|-> ($a $b) (+ $a $b)) (superpose (1 2 3)) 0)")
!(check-text maplist-with-a-lambda
    (collapse (maplist (|-> ($a) (+ 1 $a)) (1 2 3)))
    "(maplist (|-> ($a) (+ 1 $a)) (1 2 3))")


; A value may hold a variable.  What a later step binds reaches the value made
; earlier, and the code after it: the state is one atom, a rule that computes
; in a let conjunct keeps the variables of what it took, and the answers of a
; request are met with the request the state holds.
(= (single $space $pattern $template) (once (match $space $pattern $template)))
!(pm:program ((= (single $space $pattern $template) (once (match $space $pattern $template)))))
!(check let-meets-a-value-that-holds-a-variable (let ($a 3) (1 $b) ($a $b)))
!(check let-star-binds-on-both-sides (let* ((($f1 $c1 3) (1 2 $d1))) ($f1 $c1 $d1)))
!(check a-variable-bound-after-its-value-was-made (let $x (1 $b) (let $b 3 $x)))
!(check an-applied-value-that-is-data-keeps-its-variable
    (let ($a $b) (1 2) (let $y ($a $c) (let $c 5 $y))))
!(check match-binds-for-the-code-after-it
    (let $r (match &facts (edge $x $y) found) (went $r $x $y)))
!(check match-of-a-pattern-held-in-a-variable
    (let $t (edge $x c) (let $z (match &facts $t ok) (pair $z $t))))
!(check match-inside-a-match
    (match &facts (edge $x $y) (match &facts (edge $y $z) (path $x $z))))
!(check pattern-and-template-given-as-arguments
    (collapse (single &facts (edge $x $y) (seen $y))))
; negative: what one alternative binds does not reach the alternative after it
!(check each-match-binds-for-itself
    (collapse (let $r (match &facts (edge $x $y) found) (pair $x $y))))
; negative: no match, no answer; a variable met by nothing stays a variable
!(check no-match-no-answer (collapse (let $r (match &facts (edge z $y) found) $y)))
!(check a-variable-met-by-nothing-stays (let $x (1 $b) (let $c 3 $x)))

; A function run backwards: a call with a variable among its arguments meets the
; clause heads, and what a head binds reaches the code after the call.
(= (same $a $a) True)
(= (peano-plus Z $n) $n)
(= (peano-plus (S $m) $n) (S (peano-plus $m $n)))
(= (parent tom bob) True)
(= (parent bob ann) True)
!(pm:program ((= (same $a $a) True)
              (= (peano-plus Z $n) $n)
              (= (peano-plus (S $m) $n) (S (peano-plus $m $n)))
              (= (parent tom bob) True)
              (= (parent bob ann) True)))
!(check a-head-binds-the-callers-variable (let True (same $x 3) $x))
!(check a-call-enumerates-its-unbound-arguments (let True (parent $p $c) (pair $p $c)))
!(check two-calls-joined-by-a-variable
    (let True (parent $g $p) (let True (parent $p $c) (grand $g $c))))
!(check addition-run-backwards-is-subtraction
    (once (let (S (S (S Z))) (peano-plus $x (S Z)) $x)))
; negative: no head meets the call, no answer
!(check no-head-meets-the-call (collapse (let True (parent ann $c) $c)))

; A call written as a let's pattern runs toward the let's value, as on the
; hosts: a constructor in a body meets the value before its items run, so the
; search of a function run backwards ends where the value does.  The machine
; knows as constructors the symbols the program takes apart in an equation's
; head (S, box); any other head (made) runs and its answer is met with the value.
(= (enclose $x) (box $x))
(= (opened (box $x)) $x)
(= (pair-up $x $y) (made $x $y))
!(pm:program ((= (enclose $x) (box $x))
              (= (opened (box $x)) $x)
              (= (pair-up $x $y) (made $x $y))))
!(check a-call-as-a-let-pattern-runs-backwards
    (collapse (let (peano-plus $a (S Z)) (S (S (S (S Z)))) $a)))
!(check a-call-as-a-let-pattern-finds-every-split
    (collapse (let (peano-plus $a $b) (S (S Z)) (split $a $b))))
!(check a-constructor-in-the-body-meets-the-value-first (let (enclose $q) (box 7) $q))
!(check a-call-pattern-with-a-known-argument (let (pair-up $p 2) (made 1 2) $p))
!(check a-function-that-takes-a-constructor-apart (opened (enclose 5)))
; negative: the value cannot be the call's answer; no answer, and the search ends
!(check a-call-pattern-that-cannot-answer (collapse (let (pair-up $p 3) (made 1 2) $p)))
!(check a-call-pattern-against-another-constructor
    (collapse (let (peano-plus $a (S Z)) Z $a)))

; An operation of the host applied to a value that holds a variable is asked of
; the host: it may answer more than once, and each answer is an alternative
; with what it bound.
!(check or-of-an-unknown-answers-for-each-value (collapse (let $r (or $x True) (pair $x $r))))
!(check and-binds-what-makes-it-hold (collapse (if (and (or $x True) $y) (pair $x $y))))
!(check and-of-two-unknowns
    (collapse (let $r (and $x $y) (triple $x $y $r))))
; negative: with known values the operation is computed by a rule and answers once
!(check or-of-known-values-answers-once (collapse (or False True)))

; A head that is no name of the program is the host's: applied to values, a
; constructor comes back as the expression.
!(check a-constructor-applied-to-computed-values (boxed (double 2) (colour)))
!(check a-constructor-inside-a-constructor (outer (inner (double 1)) leaf))
; negative: a name of the program is called, not built
!(check a-name-of-the-program-is-called (pair (double 2) (first-colour)))

; A case row with a list view is taken only when the view opens; otherwise the
; next row is offered the value as it was.
!(check a-case-row-with-a-view-is-taken (case (1 2) (((cons $h $t) (yes $h $t)) ($else no))))
!(check a-case-row-with-two-views (case (1) (((cons $h (cons $g $t)) two) ((cons $h $t) one) ($else none))))
; negative: the view does not open, and the next row is taken
!(check a-case-row-whose-view-stays-shut (case () (((cons $h $t) (yes $h $t)) ($else no))))
!(check a-case-row-with-a-view-of-a-symbol (case a (((cons $h $t) (yes $h $t)) ($else no))))

; A call of a function of the program written in an equation's head says that
; the argument is an answer of the call: the call runs toward the argument's
; value before the body does.  An operation of the host met on the way is
; handed the value with the application, so (append (42 10) $B) toward
; (42 10 40) has one answer.
(= (prefixed $A $B) (append (append (42) $A) $B))
(= (after-ten (prefixed (10) $B) $C) ($B $C))
(= (living garfield) True)
(= (being garfield) True)
(= (small garfield) True)
(= (living snoopy) True)
(= (being snoopy) True)
(= (being roomba) True)
(= (small roomba) True)
(= (only $C $X) (let $constraint $C $X))
(= (animal $X) (only ((living $X) (being $X)) $X))
(= (cat (animal $X)) (only (small $X) $X))
(= (within $x $L) (let True (is-member $x $L) $x))
(= (ranged-plus (within $X (1 2 3)) (within $Y (2 3))) (within (+ $X $Y) (3 4 5)))
(= (twice $x) (* 2 $x))
(= (headed $X $Y) (append ($X) $Y))
(= (numeral 1) one)
(= (numeral 2) two)
!(pm:program ((= (prefixed $A $B) (append (append (42) $A) $B))
              (= (after-ten (prefixed (10) $B) $C) ($B $C))
              (= (living garfield) True) (= (being garfield) True) (= (small garfield) True)
              (= (living snoopy) True) (= (being snoopy) True)
              (= (being roomba) True) (= (small roomba) True)
              (= (only $C $X) (let $constraint $C $X))
              (= (animal $X) (only ((living $X) (being $X)) $X))
              (= (cat (animal $X)) (only (small $X) $X))
              (= (within $x $L) (let True (is-member $x $L) $x))
              (= (ranged-plus (within $X (1 2 3)) (within $Y (2 3))) (within (+ $X $Y) (3 4 5)))
              (= (twice $x) (* 2 $x))
              (= (headed $X $Y) (append ($X) $Y))
              (= (numeral 1) one)
              (= (numeral 2) two)))
!(check a-call-in-a-head-constrains-the-argument (after-ten (42 10 40) 42000))
!(check a-call-in-a-head-is-a-relation (msort (collapse (cat $X))))
!(check calls-in-a-head-in-range (collapse (ranged-plus 1 3)))
!(check calls-in-a-head-with-an-unknown (collapse (ranged-plus $x 3)))
!(check calls-in-a-head-with-two-unknowns (collapse (ranged-plus $x $y)))
!(check calls-in-a-head-under-a-let (collapse (let True (> (ranged-plus $x 2) 3) $x)))
; negative: the argument is no answer of the call
!(check a-call-in-a-head-that-cannot-answer (collapse (after-ten (41 10 40) 42000)))
!(check calls-in-a-head-answer-out-of-range (collapse (ranged-plus 3 3)))
!(check calls-in-a-head-argument-out-of-range (collapse (ranged-plus 3 4)))

; An operation of the host toward a value, and a call inside a let's pattern.
!(check a-host-operation-toward-a-list (let (headed $Head $Tail) (1 2 3 4 5 6) ($Head $Tail)))
!(check a-call-inside-a-let-pattern (collapse (let (pair (twice 4) $y) (pair 8 9) (got $y))))
; negative: the value has no such split; the inner call answers something else
!(check a-host-operation-toward-an-empty-list (collapse (let (headed $Head $Tail) () ($Head $Tail))))
!(check a-call-inside-a-let-pattern-that-differs (collapse (let (pair (twice 4) $y) (pair 7 9) (got $y))))

; A call written in a case's row runs before the rows are offered, and the row
; holds its answer.
!(check a-call-in-a-case-row (case 8 (((twice 4) eight) ($else other))))
!(check a-call-inside-a-case-row-pattern (case (p 8) (((p (twice 4)) eight) ($else other))))
!(check a-relation-in-a-case-row (collapse (case one (((numeral $n) (was $n)) ($else other)))))
!(check a-call-in-a-case-row-with-an-empty-row (case (empty) (((twice 4) eight) (Empty none))))
; negative: the key is not the call's answer
!(check a-call-in-a-case-row-that-differs (case 9 (((twice 4) eight) ($else other))))

; Declared types.  Each declaration that describes a call is an alternative
; with arguments and checks of its own: an argument of type Atom is held as
; written, one of type %Undefined% is evaluated and not checked, any other is
; evaluated and its value checked; the result is checked against the last type.
(: demand2 (-> Number Atom Expression))
(: demand2 (-> Number Atom Expression))
(= (demand2 $a $b) (got $a $b))
(: quiet (-> Atom %Undefined%))
(= (quiet $x) silent)
(: exprs (-> Expression Expression))
(= (exprs $x) (seen $x))
(: held-body (-> Number Atom))
(= (held-body $x) (witness (+ $x 1)))
(: mixed-body (-> Number Atom))
(: mixed-body (-> Number %Undefined%))
(= (mixed-body $x) (+ $x 1))
(: num-id (-> Number Number))
(= (num-id $x) $x)
(: two-ways (-> Number Atom))
(: two-ways (-> Atom Atom))
(= (two-ways $x) (way $x))
(: by-kind (-> Kind1 Kind1))
(: by-kind (-> Kind2 Kind2))
(= (by-kind $a) (if (== $a K1in) K1out (if (== $a K2in) K2out Kdefault)))
(: K1in Kind1)
(: K1out Kind1)
(: K2in Kind2)
(: K2out Kind2)
(: K3in Kind1)
(: Kdefault Kind2)
(: same-type (-> $t $t Bool))
(= (same-type $a $b) True)
(: bad-result (-> Number Symbol))
(= (bad-result $x) $x)
(: of-symbol (-> Symbol Bool))
(= (of-symbol $x) True)
(: pick-arity (-> Number Atom))
(= (pick-arity $x) (+ $x 1))
(= (pick-arity $x $y) (+ $x $y))
!(pm:type! (: demand2 (-> Number Atom Expression)))
!(pm:type! (: demand2 (-> Number Atom Expression)))
!(pm:type! (: quiet (-> Atom %Undefined%)))
!(pm:type! (: exprs (-> Expression Expression)))
!(pm:type! (: held-body (-> Number Atom)))
!(pm:type! (: mixed-body (-> Number Atom)))
!(pm:type! (: mixed-body (-> Number %Undefined%)))
!(pm:type! (: num-id (-> Number Number)))
!(pm:type! (: two-ways (-> Number Atom)))
!(pm:type! (: two-ways (-> Atom Atom)))
!(pm:type! (: by-kind (-> Kind1 Kind1)))
!(pm:type! (: by-kind (-> Kind2 Kind2)))
!(pm:type! (: same-type (-> $t $t Bool)))
!(pm:type! (: bad-result (-> Number Symbol)))
!(pm:type! (: of-symbol (-> Symbol Bool)))
!(pm:type! (: pick-arity (-> Number Atom)))
!(pm:program ((= (demand2 $a $b) (got $a $b))
              (= (quiet $x) silent)
              (= (exprs $x) (seen $x))
              (= (held-body $x) (witness (+ $x 1)))
              (= (mixed-body $x) (+ $x 1))
              (= (num-id $x) $x)
              (= (two-ways $x) (way $x))
              (= (by-kind $a) (if (== $a K1in) K1out (if (== $a K2in) K2out Kdefault)))
              (= (same-type $a $b) True)
              (= (bad-result $x) $x)
              (= (of-symbol $x) True)
              (= (pick-arity $x) (+ $x 1))
              (= (pick-arity $x $y) (+ $x $y))))
!(check a-declaration-given-twice-is-one-alternative (collapse (demand2 (+ 2 4) (+ 4 2))))
!(check an-argument-of-type-atom-does-not-run (quiet (println! SHOULD-NOT-RUN)))
!(check an-expression-value-has-the-type-expression (exprs (tag a)))
!(check a-body-is-answered-as-written-for-the-type-atom (held-body 6))
!(check two-declarations-are-two-alternatives (collapse (mixed-body 6)))
!(check each-declaration-evaluates-the-argument-for-itself (collapse (two-ways (+ 1 2))))
!(check a-number-has-the-type-number (num-id 5))
!(check a-symbol-with-a-declared-type (by-kind K1in))
!(check the-second-declaration-admits-the-call (by-kind K2in))
!(check a-type-variable-shared-by-two-arguments (collapse (same-type 1 2)))
!(check a-symbol-has-the-metatype-symbol (of-symbol foo))
!(check a-declaration-with-as-many-inputs-as-arguments (pick-arity 5))
!(check a-declaration-with-fewer-inputs-does-not-describe-the-call (pick-arity 5 6))
; negative: an argument or a result that has not the declared type
!(check a-computed-number-is-no-expression (collapse (exprs (+ 2 4))))
!(check a-symbol-is-no-number (collapse (num-id wrong)))
!(check a-result-of-another-type (collapse (by-kind K3in)))
!(check a-type-variable-bound-by-the-first-argument (collapse (same-type 1 "x")))
!(check a-number-is-no-symbol-result (collapse (bad-result 5)))
!(check a-number-has-not-the-metatype-symbol (collapse (of-symbol 5)))

; A pattern is read as an expression: an operation of the host in a let's
; pattern, an equation's head or a case's row is a call, run toward the value
; (in a row: before the rows are offered); (quote P) in a let's pattern stands
; for P as written; a form of the language in a let's pattern is evaluated.
(= (three-is (+ 1 2)) yes)
(= (before-three (append $a (3))) $a)
!(pm:program ((= (three-is (+ 1 2)) yes) (= (before-three (append $a (3))) $a)))
!(check an-operation-as-a-let-pattern (collapse (let (+ 1 2) 3 ok)))
!(check an-operation-run-backwards-in-a-let-pattern (collapse (let (append $a (3)) (1 2 3) $a)))
!(check an-operation-inside-a-let-pattern (collapse (let (pair (+ 1 2) $y) (pair 3 4) $y)))
!(check the-type-of-an-operation-as-a-pattern (let (get-type +) $t ok))
!(check a-quote-in-a-let-pattern (collapse (let (quote $x) (quote 5) $x)))
!(check a-quote-inside-a-let-pattern (collapse (let (pair (quote $x) $y) (pair 5 6) (got $x $y))))
!(check a-quote-in-a-let-pattern-holds-code (collapse (let (quote (+ $a 2)) (quote (+ 1 2)) $a)))
!(check a-number-at-the-head-of-a-pattern-is-data (collapse (let (1 $x) (1 2) $x)))
!(check a-symbol-at-the-head-of-a-pattern-is-data (collapse (let (foo $x) (foo 2) $x)))
!(check a-truth-value-at-the-head-of-a-pattern-is-data (collapse (let (True $x) (True 2) $x)))
!(check a-list-view-as-a-let-pattern (collapse (let (cons $h $t) (1 2 3) (got $h $t))))
!(check an-operation-in-a-head (collapse (three-is 3)))
!(check an-operation-run-backwards-in-a-head (collapse (before-three (1 2 3))))
!(check an-operation-in-a-case-row (collapse (case 3 (((+ 1 2) yes) ($else no)))))
!(check a-form-in-a-let-pattern (let ($x (42 (if (== $x 2) 43 44))) (3 (42 $z)) (+ $x $z)))
; negative: the value is not the operation's answer
!(check an-operation-as-a-let-pattern-that-differs (collapse (let (+ 1 2) 4 ok)))
!(check a-quoted-pattern-against-a-computed-value (collapse (let (quote (+ $a 2)) 3 $a)))
!(check an-operation-in-a-head-that-differs (collapse (three-is 4)))
!(check an-operation-in-a-case-row-that-differs (collapse (case 4 (((+ 1 2) yes) ($else no)))))
!(check a-form-in-a-let-pattern-that-differs (collapse (let ($x (if (== $x 2) 43 44)) (2 45) $x)))

; map-atom, filter-atom and foldl-atom given a variable and a body: the list is
; the machine's value, the body is the host's to run.
(= (increased $a) (+ $a 1))
!(pm:program ((= (increased $a) (+ $a 1))))
!(check a-fold-with-a-body (foldl-atom (1 2 3 4) 0 $acc $x (+ $acc $x)))
!(check a-map-with-a-body (map-atom (1 2 3) $x (+ $x 1)))
!(check a-filter-with-a-body (filter-atom (1 2 3 4 5) $x (> $x 3)))
!(check a-map-with-a-name (map-atom (1 2 3) increased))
!(check a-map-over-a-computed-list (map-atom (cdr-atom (0 1 2 3)) $x (* $x 2)))
!(check a-map-over-values-that-hold-code (map-atom ((a (+ 1 2)) (b 3)) $x (car-atom $x)))
!(check a-fold-from-a-variable-of-the-surroundings (let $k 10 (foldl-atom (1 2 3) $k $acc $x (+ $acc $x))))
; negative: nothing passes the filter
!(check a-filter-that-keeps-nothing (filter-atom (1 2 3) $x (> $x 3)))

; A space written as an expression is evaluated first; an operation that has no
; answer for its values is no answer.
(= (named-space) space_named_by_a_call)
!(pm:program ((= (named-space) space_named_by_a_call)))
!(add-atom (named-space) (kept atom))
!(check a-space-named-by-a-call (match (named-space) $a $a))
!(check an-index-in-range (index-atom (1 2 3) 1))
; negative: nothing there; no such index
!(check a-space-named-by-a-call-without-the-atom (collapse (match (named-space) (other $a) $a)))
!(check an-index-out-of-range (collapse (index-atom (1 2 3) 5)))

; When a name gains its first equation, the stored bodies that mention it are
; compiled again: what was an expression becomes a call.  A body that is running
; keeps the term it holds.  When the name loses its last equation, the call is
; an expression again.  (An effect runs once on each side, so each side has
; names of its own.)
(= (asks-on-host $x) (answer-on-host $x))
(= (asks-on-machine $x) (answer-on-machine $x))
!(pm:program ((= (asks-on-machine $x) (answer-on-machine $x))))
!(check-apart a-name-without-equations-is-an-expression
    (let ($h $v) (asks-on-host 1) (data $v))
    (let ($h $v) (asks-on-machine 1) (data $v)))
!(check-apart a-stored-body-calls-a-name-defined-in-the-same-run
    (let $u (add-atom &self (= (answer-on-host $x) (done $x))) (asks-on-host 2))
    (let $u (add-atom &self (= (answer-on-machine $x) (done $x))) (asks-on-machine 2)))
!(check-apart a-running-body-keeps-its-expression
    (collapse (let $u (add-atom &self (= (fresh-on-host $x) (+ $x 10))) (let ($h $v) (fresh-on-host 1) (data $v))))
    (collapse (let $u (add-atom &self (= (fresh-on-machine $x) (+ $x 10))) (let ($h $v) (fresh-on-machine 1) (data $v)))))
!(check-apart the-next-run-calls-it (fresh-on-host 1) (fresh-on-machine 1))
!(check-apart a-lambda-made-before-the-definition
    (let $f (|-> ($x) (late-fn-on-host $x)) (let $u (add-atom &self (= (late-fn-on-host $x) (* 10 $x))) ($f 2)))
    (let $f (|-> ($x) (late-fn-on-machine $x)) (let $u (add-atom &self (= (late-fn-on-machine $x) (* 10 $x))) ($f 2))))
; negative: the last equation removed, the stored body answers the expression again
!(check-apart a-name-that-lost-its-equations-is-an-expression-again
    (let $u (remove-atom &self (= (answer-on-host $x) $b)) (let ($h $v) (asks-on-host 3) (data $v)))
    (let $u (remove-atom &self (= (answer-on-machine $x) $b)) (let ($h $v) (asks-on-machine 3) (data $v))))

; Errors.  An operation that raises, raises on the machine; the nearest catch
; answers the error as a value, and the choices made inside it are dropped.
(= (risky $x) (+ $x "s"))
!(pm:program ((= (risky $x) (+ $x "s"))))
!(check an-error-is-caught (let (Error $e $c) (catch (+ 1 "s")) raised))
!(check an-error-inside-a-call-is-caught (let (Error $e $c) (catch (risky 1)) raised))
!(check the-code-after-a-catch-goes-on (let $r (catch (+ 1 "s")) (if (== $r 3) three other)))
!(check each-alternative-catches-its-own-error
    (collapse (let $x (superpose (1 2)) (let (Error $e $c) (catch (+ $x "s")) (caught $x)))))
!(check an-inner-catch-without-an-error (let (Error $e $c) (catch (pair (catch (+ 1 2)) (+ 1 "s"))) raised))
; negative: nothing is raised, and catch answers what its expression answers
!(check a-catch-without-an-error (catch (+ 1 2)))
!(check a-catch-keeps-every-answer (collapse (catch (superpose (1 2)))))

; An operation the machine asks of the host is asked once, as a written
; application: what it raises is raised, and a value that is code or a
; quotation is handed over as it is.
!(check a-host-operation-that-raises-is-caught (let (Error $e $c) (catch (random-float 0 "s")) raised))
!(check a-host-operation-that-raises-inside-a-let
    (let (Error $e $c) (catch (let $r (random-float 0 "s") (got $r))) raised))
!(check each-alternative-catches-its-host-error
    (collapse (let $x (superpose (1 2)) (let (Error $e $c) (catch (random-float $x "s")) (caught $x)))))
!(check a-host-operation-toward-a-value-that-raises
    (let (Error $e $c) (catch (let (random-float 0 "s") 5 met)) raised))
!(check a-value-that-is-code-is-handed-over-as-it-is (exclude-item (quote (+ 1 2)) (quote (1 (+ 1 2) 3))))
!(check a-quotation-handed-over-stays-a-quotation (exclude-item (quote (quote a)) (quote (1 (quote a) 3))))
; negative: nothing is raised; and a head that came as a value is applied as
; the hosts apply it, where an operation that raises has no answer
!(check a-host-operation-without-an-error (let $r (catch (random-float 0 1)) (and (>= $r 0) (< $r 1))))
!(check a-host-operation-with-no-answer-under-catch (collapse (catch (member 5 (1 2 3)))))
!(check a-head-that-came-as-a-value-raises-nothing (collapse (let $g random-float (catch ($g 0 "s")))))
!(check a-head-that-came-as-a-value-answers (let $g exclude-item ($g 1 (1 2 3))))

; A goal of the host's Prolog, (translatePredicate (G A1 ... An)): the
; arguments are evaluated, the host calls the goal with the values as they
; are, each way the goal holds is an alternative, and what it binds is bound
; for that alternative alone.
!(check a-goal-binds-and-the-next-goal-reads
    (progn (translatePredicate (is $x 2)) (translatePredicate (+ $x 40 $z)) (got $x $z)))
!(check a-goal-holds-in-several-ways (collapse (progn (translatePredicate (member $m (1 2 3))) $m)))
!(check a-goal-s-arguments-are-evaluated (progn (translatePredicate (= $u (+ 1 2))) (got $u)))
!(check a-goal-is-handed-data-as-it-is (progn (translatePredicate (= $u (quote (+ 1 2)))) (got $u)))
!(check a-goal-binds-for-its-alternative-alone
    (collapse (let $n (superpose (1 2)) (progn (translatePredicate (is $y (+ $n 10))) (got $n $y)))))
; negative: a goal that does not hold has no answer and ends its alternative;
; a goal Prolog does not define raises (the error's context is the host's own
; and differs between a written goal and one called as code, so it is not compared)
!(check a-goal-that-does-not-hold (collapse (translatePredicate (= 1 2))))
!(check a-goal-that-does-not-hold-ends-its-alternative
    (collapse (let $n (superpose (1 2 3)) (progn (translatePredicate (< $n 3)) $n))))
!(check an-unknown-goal-raises
    (let (Error $what $where) (catch (translatePredicate (unknown-predicate a $x))) $what))

; call and reduce make their application when it is reached: a head that has
; equations by then is called, also in a body that is running; a head without
; equations leaves the application as written.  forall asks a check of every
; answer of a generator.
(= (five) 5)
(= (two-values) 1)
(= (two-values) 2)
(= (below-two $x) (< $x 2))
(= (chosen 1) 1)
(= (chosen 2) 2)
!(pm:program ((= (five) 5) (= (two-values) 1) (= (two-values) 2) (= (below-two $x) (< $x 2))
              (= (chosen 1) 1) (= (chosen 2) 2)))
!(check call-of-a-written-application (call (double (five))))
!(check reduce-of-a-written-application (reduce (double (five))))
!(check reduce-of-an-operation (reduce (+ 1 2)))
!(check reduce-of-a-constructed-call (let $c (cons-atom double (cons-atom 4 ())) (reduce $c)))
!(check-apart call-in-a-running-body-reaches-a-new-equation
    (let $u (add-atom &self (= (made-late-on-host $x) (* $x 3)))
      (pair (let ($n $v) (made-late-on-host 2) $v) (call (made-late-on-host 2))))
    (let $u (add-atom &self (= (made-late-on-machine $x) (* $x 3)))
      (pair (let ($n $v) (made-late-on-machine 2) $v) (call (made-late-on-machine 2)))))
!(check forall-with-every-answer-passing (forall (chosen 1) below-two))
!(check forall-with-a-lambda-as-the-check (let $c (|-> ($v) (< $v 5)) (forall (two-values) $c)))
; negative: data in a variable comes back as it is; a value that is no
; application has no answer; one answer fails the check
; (call of a name without equations, and call or reduce of a quotation, are
; not checked here: the two hosts, and written code and evaluated data on one
; host, answer them differently)
!(check reduce-of-data-in-a-variable (let $c (cons-atom nothing-here (cons-atom 4 ())) (reduce $c)))
!(check reduce-of-a-number (collapse (let $c 7 (reduce $c))))
!(check forall-with-an-answer-that-fails (forall (two-values) below-two))
!(check forall-over-an-unknown-argument (forall (chosen $x) below-two))

; A lambda applied to fewer values than it has parameters is a lambda of the
; others; applied to more, its answer is applied to the rest.  An error raised
; while a rule computes (an operation applied to more values than it takes) is
; raised on the machine.
!(check a-lambda-applied-in-parts (((|-> ($x $y) (42 $x $y)) 43) 44))
!(check a-lambda-applied-to-all-its-values ((|-> ($x $y) (42 $x $y)) 43 44))
!(check a-lambda-applied-to-more-values (collapse ((|-> ($x) (|-> ($y) (pair $x $y))) 1 2)))
!(check a-part-of-a-lambda-used-twice (let $p ((|-> ($x $y) (+ $x $y)) 10) (pair ($p 1) ($p 2))))
!(check too-many-values-for-an-operation-is-an-error (repr (catch (+ 1 2 3))))
; negative: as many values as the operation takes, no error
!(check as-many-values-as-the-operation-takes (repr (catch (+ 1 2))))

; if takes its first branch for true and its other branch for every other
; value: a symbol, a number.  Without another branch there is no answer.
!(check an-if-on-a-symbol-takes-the-other-branch (if Foo yes no))
!(check an-if-on-a-number-takes-the-other-branch (if 5 yes no))
!(check an-if-on-a-symbol-without-another-branch (collapse (if Foo yes)))
; negative: true takes the first branch
!(check an-if-on-true-takes-the-first-branch (if (== 1 1) yes no))

; The lock forms.  (with_mutex LOCK E) and (transaction E) give E's first
; answer.  A transaction whose expression has no answer, or raises, leaves the
; spaces as they were; one that answers keeps its changes, and hands them to
; the transaction around it.
(pmk 1)
!(check with-mutex-gives-the-first-answer (collapse (with_mutex lock (superpose (a b)))))
!(check a-transaction-gives-the-first-answer (collapse (transaction (superpose (a b)))))
!(check a-transaction-with-no-answer-undoes-its-additions
    (progn (collapse (transaction (progn (add-atom &self (pmt 1)) (empty))))
           (collapse (match &self (pmt $x) $x))))
!(check a-transaction-with-no-answer-undoes-its-removals
    (progn (collapse (transaction (progn (remove-atom &self (pmk 1)) (empty))))
           (collapse (match &self (pmk $x) $x))))
!(check a-transaction-that-raises-undoes-its-additions
    (progn (collapse (catch (transaction (progn (add-atom &self (pmt 3)) (+ 1 a)))))
           (collapse (match &self (pmt $x) $x))))
!(check an-inner-transaction-is-undone-with-the-one-around-it
    (progn (collapse (transaction (progn (add-atom &self (pmt 5))
                                         (transaction (add-atom &self (pmt 6)))
                                         (empty))))
           (collapse (match &self (pmt $x) $x))))
; negative: a transaction that answers keeps its additions, and with_mutex
; undoes nothing
!(check a-transaction-that-answers-keeps-its-additions
    (progn (transaction (add-atom &self (pmt 2)))
           (let $found (collapse (match &self (pmt $x) $x))
             (let $_gone (remove-atom &self (pmt 2)) $found))))
!(check with-mutex-undoes-nothing
    (progn (collapse (with_mutex lock (progn (add-atom &self (pmm 1)) (empty))))
           (let $found (collapse (match &self (pmm $x) $x))
             (let $_gone (remove-atom &self (pmm 1)) $found))))

; Rollback keeps occurrence counts when the host removes one copy.
!(check duplicate-removal-rollback
 (let $s (new-space)
   (progn (add-atom $s (x 1)) (add-atom $s (x 1))
     (collapse (transaction (progn (remove-atom $s (x 1)) (empty))))
     (collapse (get-atoms $s)))))
!(check duplicate-addition-rollback
 (let $s (new-space)
   (progn (add-atom $s (x 1))
     (collapse (transaction (progn (add-atom $s (x 1)) (empty))))
     (collapse (get-atoms $s)))))

; A cut inside once, collapse or a transaction stays inside it: the other
; equations of the function still answer.
(= (cut-in-once) (once (progn (superpose (1 2)) (cut) inner)))
(= (cut-in-once) second)
(= (cut-in-collapse) (let $l (collapse (progn (superpose (1 2)) (cut) x)) (got $l)))
(= (cut-in-collapse) second)
(= (cut-in-transaction) (transaction (progn (superpose (1 2)) (cut) inner)))
(= (cut-in-transaction) second)
!(pm:program ((= (cut-in-once) (once (progn (superpose (1 2)) (cut) inner)))
              (= (cut-in-once) second)
              (= (cut-in-collapse) (let $l (collapse (progn (superpose (1 2)) (cut) x)) (got $l)))
              (= (cut-in-collapse) second)
              (= (cut-in-transaction) (transaction (progn (superpose (1 2)) (cut) inner)))
              (= (cut-in-transaction) second)))
!(check a-cut-inside-once-stays-inside (collapse (cut-in-once)))
!(check a-cut-inside-collapse-stays-inside (collapse (cut-in-collapse)))
!(check a-cut-inside-a-transaction-stays-inside (collapse (cut-in-transaction)))
; negative: a cut in the equation's own body removes the other equations
!(check a-cut-in-the-body-removes-the-other-equations (collapse (keep-first)))

; A call with more values than any equation of the name takes.  It goes
; through an equation whose answer is a partial application, which takes the
; other values; with no such answer it is the hosts' error.
(= (over-none $x) yes)
(= (over-part $x) (+ $x))
(= (over-mixed $x) (+ $x))
(= (over-mixed $x) other)
(= (over-own $x) (pair-of $x))
(= (pair-of $x $y) (pair $x $y))
(= (over-data $x) (some-data $x))
!(pm:program ((= (over-none $x) yes)
              (= (over-part $x) (+ $x))
              (= (over-mixed $x) (+ $x))
              (= (over-mixed $x) other)
              (= (pair-of $x $y) (pair $x $y))
              (= (over-own $x) (pair-of $x))
              (= (over-data $x) (some-data $x))))
!(check more-values-than-any-equation-takes-is-an-error (repr (catch (over-none a b))))
!(check a-data-body-does-not-take-more-values (repr (catch (over-data 1 2))))
!(check a-partial-body-takes-the-other-values (over-part 1 2))
!(check only-a-partial-answer-takes-the-other-values (collapse (over-mixed 1 2)))
!(check a-partial-application-of-a-program-name-takes-the-other-values (over-own 1 2))
!(check the-error-is-caught-and-the-code-goes-on
    (let $caught (catch (over-none a b)) (over-part 1 2)))
; negative: as many values as the equation takes
!(check as-many-values-as-the-equation-takes (over-none a))

; A list view is the equation of the value with the cell of its first item and
; its rest: a cell whose rest is no list opens into its two parts, and two
; views of one unknown give the same parts.
!(check a-view-of-a-cell-whose-rest-is-no-list (let (cons $a $b) (cons 1 2) (got $a $b)))
!(check two-views-of-one-unknown
    (collapse (let (cons $a $b) $x (let (cons $c $d) $x (got $a $b $c $d)))))
; negative: a symbol and the empty list have no view
!(check a-symbol-has-no-view (collapse (let (cons $a $b) foo (got $a $b))))
!(check the-empty-list-has-no-view (collapse (let (cons $a $b) () (got $a $b))))

; The program's view of &self.  The machine's equations are atoms of the host's
; &self; what the program reads from &self, counts in it or removes from it
; leaves them out.
!(check-text the-machine's-equations-are-not-in-the-program's-view
    (collapse (collapse (empty)))
    "(collapse (match &self (= (pm:answers $e) $b) found))")
!(check-text the-program-counts-its-own-atoms
    (collapse (- (size-atom (collapse (get-atoms &self))) (size-atom (collapse (get-atoms &pm-hidden)))))
    "(size-atom (collapse (get-atoms &self)))")
!(check-text a-wide-removal-leaves-the-machine
    (collapse (double 2))
    "(let $u (remove-atom &self (= (pm:answers $e) $b)) (double 2))")
!(check-text an-equation-is-an-atom-of-the-program's-space
    (collapse (collapse (superpose (found))))
    "(collapse (match &self (= (double $x) $body) found))")

; Declared rows.  Each distinct row of a name is an alternative of its call, in
; the order declared, and delivers each argument by its own parameter: as
; written to a parameter typed Atom, evaluated and checked otherwise.
(: two-rows (-> Number Number))
(: two-rows (-> $t $t))
(: held-or-not (-> Atom Atom))
(: held-or-not (-> Number Number))
(: not-or-held (-> Number Number))
(: not-or-held (-> Atom Atom))
(: number-only (-> Number Number))
(= (two-rows $x) (+ $x 1))
(= (held-or-not $x) $x)
(= (not-or-held $x) $x)
(= (number-only $x) $x)
!(pm:type! (: two-rows (-> Number Number)))
!(pm:type! (: two-rows (-> $t $t)))
!(pm:type! (: held-or-not (-> Atom Atom)))
!(pm:type! (: held-or-not (-> Number Number)))
!(pm:type! (: not-or-held (-> Number Number)))
!(pm:type! (: not-or-held (-> Atom Atom)))
!(pm:type! (: number-only (-> Number Number)))
!(pm:program ((= (two-rows $x) (+ $x 1))
              (= (held-or-not $x) $x)
              (= (not-or-held $x) $x)
              (= (number-only $x) $x)))
!(check each-distinct-row-answers (two-rows 1))
!(check each-row-delivers-by-its-own-demand (held-or-not (+ 1 2)))
!(check rows-answer-in-the-order-declared (not-or-held (+ 1 2)))
!(check an-argument-of-another-type-gives-no-answer (number-only (superpose (1 a 2))))

; A request under once.  The operand of once that is an application by the
; host is asked for its first answer: the host's relation may have no last one.
(= (host-counts-from $n) $n)
(= (host-counts-from $n) (host-counts-from (+ $n 1)))
!(check once-asks-the-host-for-its-first-answer (once (host-counts-from 1)))
!(check once-of-a-host-request-in-a-let (let $n (once (host-counts-from 5)) (+ $n 1)))

; A request answered in turn.  The host's answers are taken one at a time, the
; host's own enumeration the alternative: a relation with no last answer is
; answered as far as the program asks, and an error the host raises after some
; answers is raised after them.
(= (host-three) a)
(= (host-three) b)
(= (host-three) c)
(= (host-two-then-raise) 1)
(= (host-two-then-raise) 2)
(= (host-two-then-raise) (+ 1 x))
!(check an-endless-host-relation-is-asked-as-far-as-needed
    (once (let $n (host-counts-from 1) (if (> $n 2) $n (empty)))))
!(check host-answers-come-in-order (host-three))
!(check host-enumerations-nest (let $x (host-three) (let $y (host-three) (pair $x $y))))
!(check a-failed-alternative-goes-back-to-the-host (let $x (host-three) (if (== $x b) (empty) $x)))
!(check the-host's-error-is-raised-after-its-answers (collapse (catch (host-two-then-raise))))
!(check once-inside-an-enumeration
    (collapse (let $n (once (let $m (host-counts-from 1) (if (> $m 3) $m (empty))))
                (let $k (host-three) (got $n $k)))))
!(check a-cut-drops-the-rest-of-the-host's-answers (once (let $x (host-three) (pair $x (host-three)))))

; A call selects the clauses whose heads meet its arguments.  They are taken in
; their order; none is no answer.
(= (meets a) 1)
(= (meets b) 2)
(= (meets a) 3)
(= (meets $x) 4)
(= (meets-one a) 1)
!(pm:program ((= (meets a) 1) (= (meets b) 2) (= (meets a) 3) (= (meets $x) 4) (= (meets-one a) 1)))
!(check clauses-that-meet-are-taken-in-order (collapse (meets a)))
!(check one-clause-meets (collapse (meets b)))
!(check the-open-clause-meets-what-no-other-does (collapse (meets c)))
!(check no-clause-meets-no-answer (collapse (meets-one c)))
!(check an-unknown-meets-every-clause (collapse (let $r (meets $u) (pair $u $r))))

; Arguments are evaluated left to right, a literal with no step of its own.
!(check literal-and-computed-arguments-keep-their-order (triple 1 (double 2) 3 (double 4)))
!(check arguments-with-choices-keep-their-order (collapse (pair3 (superpose (1 2)) 0 (superpose (a b)))))

; A call that is the last thing its caller does takes the caller's frame.  A
; cut in the clause called that way stays in that clause.
(= (count-down $n) (if (== $n 0) done (count-down (- $n 1))))
(= (count-up $n) (if (== $n 0) 0 (+ 1 (count-up (- $n 1)))))
(= (cut-inside $x) (progn (superpose (1 2)) (cut) $x))
(= (cut-inside $x) never)
(= (calls-last) (cut-inside 7))
(= (calls-last) second)
!(pm:program ((= (count-down $n) (if (== $n 0) done (count-down (- $n 1))))
              (= (count-up $n) (if (== $n 0) 0 (+ 1 (count-up (- $n 1)))))
              (= (cut-inside $x) (progn (superpose (1 2)) (cut) $x))
              (= (cut-inside $x) never)
              (= (calls-last) (cut-inside 7))
              (= (calls-last) second)))
!(check a-loop-written-as-a-call (count-down 60))
!(check a-cut-in-a-clause-called-last-stays-in-that-clause (collapse (calls-last)))

; A choice takes its items in turn; the last runs with no choice point behind it.
!(check a-choice-of-one-item (superpose (7)))
!(check a-choice-takes-its-items-in-order (collapse (superpose (1 2 3))))
!(check a-cut-in-the-last-item-of-a-choice (collapse (cut-inside (superpose (5 6)))))

; What a run stopped by the limit has left behind: its choice points, and the
; frames of its continuation.
(= (alternatives-now) (size-atom (collapse (match &pm (pm:alt $d $c $k) alt))))
; The frames of a continuation: those the state keeps, and those below them in
; rows of their own.
(= (frames-of $k)
   (if (== $k pm:halt) 0
     (if (== (car-atom $k) pm:top)
         (let ($_top $id $link) $k (frames-of (frames-below $id)))
         (let ($_k $frame $rest) $k (+ 1 (frames-of $rest))))))
(= (frames-below $id) (car-atom (collapse (match &pm (pm:below $id $l $frames) $frames))))
(= (kept-of $k)
   (if (== $k pm:halt) 0
     (if (== (car-atom $k) pm:top) 0
         (let ($_k $frame $rest) $k (+ 1 (kept-of $rest))))))
(= (frames-now) (car-atom (collapse (match &pm (pm:state $c $k $d $r) (frames-of $k)))))
(= (kept-now) (car-atom (collapse (match &pm (pm:state $c $k $d $r) (kept-of $k)))))
(= (rows-below-now) (size-atom (collapse (match &pm (pm:below $id $l $frames) below))))
(: check-stopped (-> Atom Atom Atom Atom %Undefined%))
(= (check-stopped $name $steps $expression $observation)
   (let* (($_set (pm:limit! $steps))
          ($machine (catch (pm:answers $expression)))
          ($_unset (pm:limit! none))
          ($seen (eval $observation))
          ($_shown (println! (machine $name (stopped $machine $seen)))))
     (if (and (=alpha (pm:no-end pm:out-of-fuel) $machine) (== $seen True)) (holds $name (pm:steps))
         (FAILS $name (stopped (pm:no-end pm:out-of-fuel) True) (stopped $machine $seen)))))
; one clause meets each call of count-up: no choice point is left
!(check-stopped a-call-one-clause-meets-leaves-no-choice-point 400 (count-up 60)
    (== (alternatives-now) 0))
; negative: three clauses meet (colour); the later two wait behind one choice point
!(check-stopped a-call-several-clauses-meet-leaves-one-choice-point 400
    (pair (colour) (count-up 60)) (== (alternatives-now) 1))
; the last item of a choice leaves none: after 1 and 2 are answered the run is in 3
!(check-stopped the-last-item-of-a-choice-leaves-no-choice-point 1300
    (let $x (superpose (1 2 3)) (pair $x (count-up 20))) (== (alternatives-now) 0))
; negative: while an earlier item runs, the later ones wait behind one
!(check-stopped an-earlier-item-of-a-choice-leaves-one-choice-point 400
    (let $x (superpose (1 2 3)) (pair $x (count-up 20))) (== (alternatives-now) 1))
; the loop holds its one clause frame however far it has run
!(check-stopped a-loop-written-as-a-call-keeps-one-frame 700 (count-down 60) (< (frames-now) 4))
; negative: a call with work left after it keeps its frames
!(check-stopped a-call-with-work-after-it-keeps-its-frames 700 (count-up 60) (> (frames-now) 20))
; The state keeps the newest frames only; the others are in rows of their own.
!(check-stopped the-state-keeps-the-newest-frames 700 (count-up 60) (< (kept-now) 19))
!(check-stopped the-frames-below-are-rows-of-their-own 700 (count-up 60) (> (rows-below-now) 2))
; negative: the loop's one frame never leaves the state
!(check-stopped a-loop-moves-no-frames-out 700 (count-down 60) (== (rows-below-now) 0))
; What is bound above reaches the frames below when they come back: $v is
; unknown when the recursion starts, bound at its bottom, and read by the frame
; that waited longest.
(= (binds-at-the-bottom $n $x)
   (if (== $n 0) (let $x found done) (let $r (binds-at-the-bottom (- $n 1) $x) $r)))
(= (chooses-at-the-bottom $n $x)
   (if (== $n 0) (let $x (superpose (a b)) done) (let $r (chooses-at-the-bottom (- $n 1) $x) $r)))
!(pm:program ((= (binds-at-the-bottom $n $x)
                 (if (== $n 0) (let $x found done) (let $r (binds-at-the-bottom (- $n 1) $x) $r)))
              (= (chooses-at-the-bottom $n $x)
                 (if (== $n 0) (let $x (superpose (a b)) done)
                     (let $r (chooses-at-the-bottom (- $n 1) $x) $r)))))
!(check a-binding-made-above-reaches-the-frames-below (pair (binds-at-the-bottom 40 $v) $v))
; negative: the frames below are read afresh by each alternative; what one binds
; the other does not see
!(check each-alternative-reads-the-frames-below-afresh
    (collapse (pair (chooses-at-the-bottom 40 $v) $v)))
; a cut and a caught error find their frame among the frames below
(= (cuts-after $n) (if (== $n 0) (progn (cut) reached) (let $r (cuts-after (- $n 1)) $r)))
(= (cut-far-below) (progn (superpose (1 2)) (cuts-after 40)))
(= (cut-far-below) never)
(= (raises-after $n) (if (== $n 0) (+ 1 "s") (let $r (raises-after (- $n 1)) $r)))
!(pm:program ((= (cuts-after $n) (if (== $n 0) (progn (cut) reached) (let $r (cuts-after (- $n 1)) $r)))
              (= (cut-far-below) (progn (superpose (1 2)) (cuts-after 40)))
              (= (cut-far-below) never)
              (= (raises-after $n) (if (== $n 0) (+ 1 "s") (let $r (raises-after (- $n 1)) $r)))))
!(check a-cut-below-many-frames-stays-in-its-clause (collapse (cut-far-below)))
!(check an-error-is-caught-across-the-frames-below (collapse (catch (raises-after 40))))

; A run driven in turns gives the answers of the run driven at once and fires
; the same number of rules: the machine's whole state is rows of the store.
(= (turns-until-ended $steps $taken)
   (if (== (pm:advance $steps) pm:turn-over) (turns-until-ended $steps (+ $taken 1)) (+ $taken 1)))
(: check-turns (-> Atom Atom Atom %Undefined%))
(= (check-turns $name $steps $expression)
   (let* (($host (collapse (eval $expression)))
          ($at-once (catch (pm:answers $expression)))
          ($fired-at-once (pm:steps))
          ($_begun (pm:begin $expression))
          ($turns (turns-until-ended $steps 0))
          ($machine (pm:end))
          ($_shown (println! (machine $name (in-turns $turns $machine)))))
     (if (and (=alpha $host $machine)
              (and (=alpha $at-once $machine) (and (== (pm:steps) $fired-at-once) (> $turns 2))))
         (holds $name (pm:steps))
         (FAILS $name (in-turns $host $fired-at-once) (in-turns $turns $machine (pm:steps))))))
!(check-turns a-run-in-turns-answers-as-the-run-at-once 100 (fib 9))
!(check-turns choice-points-outlive-a-turn 40 (pair (colour) (count-up 12)))
!(check-turns a-collapse-outlives-a-turn 40 (collapse (pair (colour) (count-up 8))))
; negative: a run that has not ended has no answers yet, and says so
(: check-unended (-> Atom Atom Atom %Undefined%))
(= (check-unended $name $steps $expression)
   (let* (($_begun (pm:begin $expression))
          ($outcome (pm:advance $steps))
          ($machine (pm:end))
          ($_finish (pm:advance none))
          ($_shown (println! (machine $name (unended $outcome $machine)))))
     (if (and (== $outcome pm:turn-over) (=alpha (pm:no-end pm:turn-over) $machine))
         (holds $name (pm:steps))
         (FAILS $name (unended pm:turn-over (pm:no-end pm:turn-over)) (unended $outcome $machine)))))
!(check-unended a-run-that-has-not-ended-says-so 50 (fib 9))

; The answers so far are a row of their own, (pm:given ANSWERS): the state does
; not carry them through every rule.
(= (given-now) (car-atom (collapse (match &pm (pm:given $answers) (pm:items-length $answers)))))
(= (state-carries-now)
   (car-atom (collapse (match &pm (pm:state $c $k $d $r) $r))))
; after 1 and 2 are answered the run is in 3: two answers are given so far
!(check-stopped the-answers-so-far-are-a-row-of-their-own 1300
    (let $x (superpose (1 2 3)) (pair $x (count-up 20))) (== (given-now) 2))
; negative: the state carries none of them
!(check-stopped the-state-carries-no-answers 1300
    (let $x (superpose (1 2 3)) (pair $x (count-up 20))) (== (state-carries-now) pm:nil))

; The driver's step limit is a setting.  There is none by default; a run that
; reaches a set limit stops with pm:out-of-fuel and gives no answer of its own,
; and once the limit is removed the same run finishes.
(: check-limited (-> Atom Atom Atom %Undefined%))
(= (check-limited $name $steps $expression)
   (let* (($_set (pm:limit! $steps))
          ($machine (catch (pm:answers $expression)))
          ($_unset (pm:limit! none))
          ($_shown (println! (machine $name $machine))))
     (if (=alpha (pm:no-end pm:out-of-fuel) $machine) (holds $name (pm:steps))
         (FAILS $name (pm:no-end pm:out-of-fuel) $machine))))
!(check-limited a-set-limit-stops-the-run 3 (fib 7))
!(check a-removed-limit-lets-the-run-finish (fib 7))
