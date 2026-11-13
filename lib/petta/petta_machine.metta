; A PeTTa evaluator whose every step is a transform rule.
;
; lib_petta_by_transform and transform_only keep a program's control with the
; host: calls and space operations become firings, the evaluator stays the
; client of the store.  Here the evaluator is in the store too.  The machine's
; state is one stored atom; each of its transitions is one rule
;
;     (pm:rule KEY SOURCES TEMPLATE)
;
; a row of the table &pm-rules, fired as (transform &pm SOURCES TEMPLATE &pm):
; it takes the state, reads what it needs, and makes the next state.  A driver
; fires the rules of the state's key until the machine is done.  (The same rules
; can be fired by the host's own match, remove-atom and add-atom: see "how a rule
; is fired".)  A program is
; data for these rules: its equations are rows (pm:clause F INDEX HEAD BODY),
; each with its head again in a row (pm:heads F INDEX HEAD).
;
;   (pm:state CONTROL CONTINUATION DEPTH pm:nil)      the machine's state
;                                                     CONTINUATION: pm:halt, (pm:k
;                                                     FRAME CONTINUATION), or (pm:top
;                                                     ID LINK) where the frames the
;                                                     state keeps end
;   (pm:alt DEPTH CONTROL CONTINUATION)               a choice point, a row of
;                                                     its own: a stored row is
;                                                     a copy, so what the taken
;                                                     branch binds later does
;                                                     not reach it
;   (pm:below ID LINK FRAMES)                         the frames of the continuation
;                                                     below those the state keeps
;   (pm:bag DEPTH ITEMS)                              the answers a collapse has
;                                                     so far, a row of its own:
;                                                     it outlives backtracking
;   (pm:unseen DEPTH)                                 a case with an Empty row
;                                                     has had no value yet
;   (pm:open-journals JOURNALS)                       the transactions open now,
;                                                     each the changes made to
;                                                     spaces while it ran
;   (pm:given ANSWERS)                                the answers so far, newest first.
;                                                     The state's fourth field is not
;                                                     used for them and stays pm:nil.
;   (pm:done ANSWERS)                                 the machine has halted
;
; CONTROL   (pm:eval TERM)  (pm:ret VALUE)  pm:fail
;           (pm:evlis TODO DONE TARGET)  (pm:ready VALUES TARGET)
;           (pm:try F ARGS)  (pm:try-each F ARGS INDICES)  (pm:enter F ARGS INDEX DEPTH)
;           (pm:effect REQUEST)
;           (pm:answers-of REQUEST ANSWERS)  (pm:answer-of REQUEST ANSWER)
;           (pm:more-of ID REQUEST)
;           (pm:eval-to TERM VALUE)  (pm:each-to PAIRS VALUE)
;           (pm:try-to F ARGS VALUE)  (pm:try-each-to F ARGS INDICES VALUE)
;           (pm:enter-to F ARGS INDEX VALUE DEPTH)
;           (pm:chains F TERMS WRITTEN SIGNATURES)
;           (pm:targs TERMS WRITTEN DEMANDS TYPES DONE TARGET)
;           (pm:select VALUE ROWS)  (pm:meet-row VALUE PATTERN CODE)  (pm:collected DEPTH)
;           (pm:cut-find CONTINUATION)  (pm:cut-to DEPTH)
;           (pm:raise ERROR)  (pm:raise-find ERROR CONTINUATION)  (pm:raise-to DEPTH ERROR)
;           (pm:uncaught ERROR)  (pm:tx-failed)  (pm:over-end ERROR)  (pm:over-seen)
; TERM      (pm:lit V)  (pm:if C T E)  (pm:let P E B)  (pm:choice ITEMS)
;           pm:empty  (pm:data F ITEMS)  (pm:call F ITEMS)  (pm:prim OP ITEMS)
;           (pm:tcall F ITEMS WRITTEN)  (pm:hostc F ITEMS)  (pm:commit CODE)
;           (pm:host F ITEMS)  (pm:apply ITEMS)  (pm:each E)  (pm:reval E)
;           (pm:view VARIABLE H T CODE)  (pm:toward TERM VALUE CODE)
;           (pm:add S A)  (pm:remove S A)  (pm:match S P T)  (pm:atoms S)
;           (pm:once E)  (pm:collapse E)  (pm:case E ROWS)  (pm:seq A B)  pm:cut
;           (pm:tx E)
;           (pm:case-e E ROWS EMPTY-BODY)
; ITEMS     pm:nil  (pm:items FIRST REST)
; INDEX     a number: the clause's place among its name's clauses
; DEPTH     z  (s DEPTH)
;
; A let binds by a take: the rule's pattern names the returned value and the
; let's pattern with one variable, so the state is taken only when they unify.
; Clause choice is a read of the head rows that meet the call's arguments: the one
; such clause is entered with no choice point; of several, the later ones wait
; behind one.
; Backtracking takes the newest choice point.  What a rule cannot do by
; matching it computes in a `let` conjunct: applying a grounded operation and
; building a data expression.  A request on a space (add, remove, match) is
; handed to the driver, which performs it between rules; an effect inside a
; `let` conjunct would run when the rule is proposed and again when it fires.
; The driver collects the answers of a read of a space, and takes the answers
; of an application by the host one at a time (see "a request answered in
; turn"); either way an answer is a copy with variables of its own.  So each answer is kept beside the instance of the
; request that gave it, and the rule that takes an answer names that instance
; and the request the state holds with one variable: taking the state binds what
; the answer bound.  The choice point for the answers after it is made first.
;
; once keeps the depth it was entered at; its first answer takes the choice
; points made above that depth, one rule firing each, so the alternatives they
; hold never run.  collapse keeps its answers in a row of its own and leaves a
; choice point that returns them when the expression has no answer left.
; case offers its value to the rows in order.  A clause's body runs under a
; frame that keeps the depth the call was made at; cut finds the nearest such
; frame and takes the choice points above it.
;
; A head that is no equation of the program and no form of the machine is the
; host's: its arguments are evaluated by the machine and the host applies it
; to the values (a request, performed between rules), every answer an
; alternative; data comes back as it went.  A value applied to values is
; called when it names equations of the program, and is the host's otherwise.
; An operation of the host that itself calls a function of the program does
; that on the host.  A declaration given to pm:type! types the name's calls:
; each declaration that describes a call is an alternative; an argument is held
; as written (type Atom), evaluated (%Undefined%), or evaluated and checked
; against its type, and the result is checked; an equation's body is answered
; as written when its declaration answers Atom.  Whether a value has a type is
; asked of the host, whose relation get-type a program can extend.
; An equation added to &self or removed from it while the program runs
; becomes or stops being a clause row, and when a name gains its first equation
; or loses its last, the stored bodies that mention it are compiled again; a
; body that is running keeps its term.  A name of the program applied to fewer
; values than its equations take is the value (partial NAME VALUES), which can
; be applied in turn; applied to more, its answer is applied to the rest.
;
; A pattern (of an equation's head, a let, a case row) may use PeTTa's list
; view (cons H T), and may hold a call of a function of the program or of an
; operation of the host: what stands there is an answer of the call, which runs
; toward that value (in a case row: before the rows are offered).  A case row
; with a list view is taken only if the view opens.
; A lambda (|-> PARAMETERS BODY) is a value; applied, its
; body runs on the machine.  Handed to an operation of the host it is an
; expression, which the host does not apply.
;
; Scope: literals, if (with or without an else), let, let*, chain, superpose
; and hyperpose (of a written or a computed list), empty, quote, eval, data,
; calls of the program's equations with plain patterns in their heads,
; applied values, lambdas, cons, foldall, maplist, prog1, the host's operations (those that answer one value and
; change nothing are computed by a rule, the list pm:primitives; the others
; are requests), unique, union, intersection, subtraction, add-atom,
; remove-atom, match, once, collapse, case (with or without an Empty row),
; progn, cut, catch and errors raised by the host's operations, call, reduce,
; forall, declared types, and map-atom, filter-atom and foldl-atom with a body.  Not provided for: a cut inside a collapse of the same body; a
; declaration given after the equations that call its name were loaded; an
; argument of type Atom of a call made through a value (it was evaluated before
; the name was known); the checks of a partial application before it is
; completed; a constraint the host's solver holds on a variable, which does not
; outlive the request that made it.
;
; A value may hold a variable.  The state is one atom and its variables are the
; program's: what a let, a clause head, a match or an application by the host
; binds reaches the values made before it and the code after it, for that
; alternative alone.  This needs a transform that keeps the variables of the
; atom a rule takes in what the rule makes, a value computed in a `let`
; conjunct included.
;
;   (pm:program (EQUATION ...))    load equations as clause rows
;   (pm:type! (: NAME TYPE))       a declaration of the program, before the
;                                  equations that call NAME
;   (pm:answers EXPRESSION)        the answers of EXPRESSION, in order, as a list
;   (pm:begin EXPRESSION)  (pm:advance STEPS)  (pm:end)    the same run driven in turns
;   (pm:steps)                     how many rules the last query fired
;   (pm:lane! direct)              fire the rules by the host's own operations;
;                                  (pm:lane! transform), the default, by transform

; Import a transform library before this one: (library lib_transform), or
; (library lib_transform_specs) for transform written in PeTTa.

!(bind! &pm (new-space))
!(bind! &pm-rules (new-space))
!(bind! &pm-count (new-state 0))
!(bind! &pm-limit (new-state none))
!(bind! &pm-turn-end (new-state none))
!(bind! &pm-outcome (new-state pm:turn-over))
!(bind! &pm-copies (new-space))
!(bind! &pm-turns (new-state 0))

; The machine's own atoms.  The machine's equations are atoms of the host's
; &self, where the host keeps code, beside the program's atoms and the
; libraries'.  What that space holds when the machine has loaded is not the
; program's: each such atom is kept in &pm-hidden as (pm:own ATOM), and what the
; program reads from &self, counts in it or removes from it leaves those atoms
; out (pm:hide-loaded!, called again by a driver that defines helpers of its
; own).  A program is loaded after the machine.
!(bind! &pm-hidden (new-space))

;; ---- small helpers ---------------------------------------------------------------

(= (pm:list2 $a $b) (cons-atom $a (cons-atom $b ())))
(= (pm:list3 $a $b $c) (cons-atom $a (cons-atom $b (cons-atom $c ()))))
(= (pm:list4 $a $b $c $d) (cons-atom $a (cons-atom $b (cons-atom $c (cons-atom $d ())))))

; ITEMS of a list, and back.
(= (pm:items-of $list)
   (if (== $list ()) pm:nil
     (let ($first $rest) (decons-atom $list)
       (pm:list3 pm:items $first (pm:items-of $rest)))))

(= (pm:list-of $items)
   (if (== $items pm:nil) ()
     (let ($_items $first $rest) $items
       (let $tail (pm:list-of $rest) (cons-atom $first $tail)))))

(= (pm:reversed $items $onto)
   (if (== $items pm:nil) $onto
     (let ($_items $first $rest) $items
       (pm:reversed $rest (cons-atom $first $onto)))))

(= (pm:peano $n) (if (== $n 0) z (pm:list2 s (pm:peano (- $n 1)))))

; The indices of collected head rows (pm:heads F INDEX HEAD), least first.
(= (pm:indices-in-order $heads) (pm:items-of (msort (pm:indices-of $heads))))

(= (pm:indices-of $heads)
   (if (== $heads ()) ()
     (let ($row $more) (decons-atom $heads)
       (let ($_h $_f $index $_head) $row
         (let $tail (pm:indices-of $more) (cons-atom $index $tail))))))

;; ---- terms: a program's expressions as data for the rules ---------------------------

; (held as data: written bare, the list would be a call of +)
; The host's operations a rule computes: each answers one value and changes
; nothing, so computing it when the rule is proposed and again when it fires
; is harmless.  Operations with several answers or with effects are requests
; to the driver, not members of this list.
(= (pm:primitives)
   (quote (+ - * / % < > <= >= == != and or not min max abs
           cons-atom car-atom cdr-atom decons-atom size-atom index-atom
           sort-atom msort unique-atom union-atom intersection-atom subtraction-atom
           reverse append length first last is-member =alpha
           repr parse get-metatype
           pow-math sqrt-math abs-math log-math trunc-math ceil-math floor-math round-math
           sin-math asin-math cos-math acos-math tan-math atan-math isnan-math isinf-math
           min-atom max-atom sum-atom)))

(= (pm:defined $name)
   (not (== (collapse (match &pm (pm:fun $name) known)) ())))

; PeTTa's list view.  In a pattern, (cons H T) meets a list that is not empty,
; H its first item and T the others.  An expression has no form for "a first
; item and the others", so the pattern is written apart: where (cons H T)
; stands, a fresh variable, and with the code the pattern guards a view
; (pm:view VARIABLE H T CODE), which a rule opens before the code runs.
(= (pm:fresh-variable) $fresh)

(: pm:is-view (-> Atom %Undefined%))
(= (pm:is-view $p)
   (if (== (size-atom $p) 3)
       (let $head (car-atom $p)
         (if (== (get-metatype $head) Variable) False (== $head cons)))
       False))

; A call of a function of the program written in a pattern, as in
; (= (h (myfunc (10) $B) $C) ...), says that what stands there is an answer of
; the call.  It is written apart in the same way: a fresh variable where it
; stands, and (pm:c VARIABLE CALL) among the views; the code the pattern guards
; runs the call toward the variable's value first (pm:toward).
; (pm:plain PATTERN VIEWS): the pattern without list views and calls, and those.
; WHERE the pattern stands is pm:head (an equation's head, a case's row) or
; pm:let: a let's pattern is read as an expression, so (quote P) there stands
; for P as written.
(: pm:plain-pattern (-> %Undefined% Atom %Undefined%))
(= (pm:plain-pattern $where $p)
   (if (== (get-metatype $p) Expression)
       (if (== $p ()) (pm:list3 pm:plain () ())
         (if (pm:is-view $p)
             (let ($_cons $h $t) $p
               (let* (($l (pm:fresh-variable))
                      ((pm:plain $h2 $views-h) (pm:plain-pattern $where $h))
                      ((pm:plain $t2 $views-t) (pm:plain-pattern $where $t))
                      ($later (pm:appended $views-h $views-t)))
                 (pm:list3 pm:plain $l (cons-atom (pm:list4 pm:v $l $h2 $t2) $later))))
             (if (or (pm:call-pattern? $p) (pm:form-in? $where $p))
                 (let* (($l (pm:fresh-variable)) ($call (pm:term $p)))
                   (pm:list3 pm:plain $l (cons-atom (pm:list3 pm:c $l $call) ())))
                 (if (pm:quoted-in? $where $p)
                     (let ($_quote $held) $p (pm:list3 pm:plain $held ()))
                     (pm:plain-each $where $p)))))
       (pm:list3 pm:plain $p ())))

; In a let's pattern a form of the language is evaluated like a call:
; (let ($x (if (== $x 2) 43 44)) (3 $z) ...) binds $z to 44.
(: pm:form-in? (-> %Undefined% Atom %Undefined%))
(= (pm:form-in? $where $p)
   (if (== $where pm:let)
       (let $head (car-atom $p)
         (if (== (get-metatype $head) Symbol)
             (is-member $head (quote (if let let* chain case progn prog1 once collapse)))
             False))
       False))

(: pm:quoted-in? (-> %Undefined% Atom %Undefined%))
(= (pm:quoted-in? $where $p)
   (if (== $where pm:let)
       (if (== (size-atom $p) 2)
           (let $head (car-atom $p)
             (if (== (get-metatype $head) Variable) False (== $head quote)))
           False)
       False))

(: pm:plain-each (-> %Undefined% Atom %Undefined%))
(= (pm:plain-each $where $items)
   (if (== $items ()) (pm:list3 pm:plain () ())
     (let ($item $more) (decons-atom $items)
       (let* (((pm:plain $item2 $views1) (pm:plain-pattern $where $item))
              ((pm:plain $more2 $views2) (pm:plain-each $where $more))
              ($views (pm:appended $views1 $views2)))
         (pm:list3 pm:plain (cons-atom $item2 $more2) $views)))))

(= (pm:appended $front $back)
   (if (== $front ()) $back
     (let ($item $more) (decons-atom $front)
       (let $tail (pm:appended $more $back) (cons-atom $item $tail)))))

; The code a pattern guards: under the pattern's list views, which only take
; apart, and inside them its calls, each in the order written.
(= (pm:with-views $views $code)
   (pm:wrapped (pm:views-of pm:v $views) (pm:wrapped (pm:views-of pm:c $views) $code)))

(= (pm:views-of $kind $views)
   (if (== $views ()) ()
     (let ($view $more) (decons-atom $views)
       (let $tail (pm:views-of $kind $more)
         (if (== (car-atom $view) $kind) (cons-atom $view $tail) $tail)))))

(= (pm:wrapped $views $code)
   (if (== $views ()) $code
     (let ($view $more) (decons-atom $views)
       (let $inner (pm:wrapped $more $code)
         (if (== (car-atom $view) pm:v)
             (let (pm:v $l $h $t) $view (cons-atom pm:view (pm:list4 $l $h $t $inner)))
             (let (pm:c $l $call) $view (pm:list4 pm:toward $call $l $inner)))))))

; (let P E B) as a term.
(: pm:let-term (-> Atom %Undefined% %Undefined% %Undefined%))
(= (pm:let-term $p $value $body)
   (if (pm:call-pattern? $p)
       (let $v (pm:fresh-variable)
         (let $toward (pm:list4 pm:toward (pm:term $p) $v $body)
           (pm:list4 pm:let $v $value $toward)))
       (let (pm:plain $p2 $views) (pm:plain-pattern pm:let $p)
         (pm:list4 pm:let $p2 $value (pm:with-views $views $body)))))

; A pattern that is a call: its head is a function of the program or an
; operation of the host, and it is no list view.  The hosts read a pattern as an
; expression, so (let (+ 1 2) 3 ok) answers ok and (= (hp (+ 1 2)) yes) takes 3.
; The call runs toward the value the pattern stands for.
(: pm:call-pattern? (-> Atom %Undefined%))
(= (pm:call-pattern? $p)
   (if (== (get-metatype $p) Expression)
       (if (== $p ()) False
         (if (pm:is-view $p) False
           (let $head (car-atom $p)
             (let $kind (get-metatype $head)
               (if (== $kind Symbol) (pm:defined $head)
                 (if (== $kind Grounded) (pm:operation? $head) False))))))
       False))

; A name the host knows as an operation has the metatype Grounded there, as a
; number, a string and a truth value have; those have a type.
(= (pm:operation? $head)
   (if (pm:defined $head) True
     (let $types (collapse (get-type $head))
       (if (is-member Number $types) False
         (if (is-member String $types) False
           (if (is-member Bool $types) False True))))))

(: pm:term (-> Atom %Undefined%))
(= (pm:term $e)
   (if (== (get-metatype $e) Expression)
       (if (== $e ()) (pm:list2 pm:lit ())
         (let $head (car-atom $e)
           (if (is-member (get-metatype $head) (Symbol Grounded))
               (let $args (cdr-atom $e) (pm:form $head $args $e))
               ; a head that is a variable or an expression is a value to apply
               (pm:list2 pm:apply (pm:terms $e)))))
       (pm:list2 pm:lit (pm:literal $e))))

; True and False are written values; the grounded operations answer true, false.
(= (pm:literal $e) (if (== $e True) true (if (== $e False) false $e)))

(: pm:atoms-form? (-> Atom Atom %Undefined%))
(= (pm:atoms-form? $head $args) (if (== $head get-atoms) (== (size-atom $args) 1) False))

(: pm:terms (-> Atom %Undefined%))
(= (pm:terms $es)
   (if (== $es ()) pm:nil
     (let ($e $more) (decons-atom $es)
       (let $one (pm:term $e)
         (let $rest (pm:terms $more) (pm:list3 pm:items $one $rest))))))

(: pm:form (-> Atom Atom Atom %Undefined%))
(= (pm:form $head $args $e)
   (if (== $head if)
       (if (== (size-atom $args) 2)
           (let ($c $t) $args (pm:list4 pm:if (pm:term $c) (pm:term $t) pm:empty))
           (let ($c $t $else) $args (pm:list4 pm:if (pm:term $c) (pm:term $t) (pm:term $else))))
   (if (== $head let)
       (let ($p $value $body) $args
         (let $first (pm:term $value) (let $rest (pm:term $body) (pm:let-term $p $first $rest))))
   (if (== $head let*)
       (let ($pairs $body) $args (pm:lets $pairs $body))
   (if (== $head chain)
       (let ($value $variable $body) $args
         (pm:list4 pm:let $variable (pm:term $value) (pm:term $body)))
   (if (== $head cons) (pm:list3 pm:prim cons-atom (pm:terms $args))
   (if (== $head |->) (pm:list2 pm:lit $e)
   (if (is-member $head (quote (superpose hyperpose)))
       (let ($choices) $args (pm:choices $choices))
   (if (== $head empty) pm:empty
   (if (== $head quote) (let ($held) $args (pm:list2 pm:lit $held))
   (if (== $head eval)
       (let ($code) $args
         (if (== (get-metatype $code) Variable)
             (pm:list2 pm:reval (pm:term $code))
             (pm:term $code)))
   (if (== $head once)
       (let ($e1) $args (pm:list2 pm:once (pm:term $e1)))
   (if (== $head with_mutex)
       (let ($_lock $e1) $args (pm:list2 pm:once (pm:term $e1)))
   (if (== $head transaction)
       (let ($e1) $args (pm:list2 pm:tx (pm:term $e1)))
   (if (== $head collapse)
       (let ($e1) $args (pm:list2 pm:collapse (pm:term $e1)))
   (if (== $head catch)
       (let ($e1) $args (pm:list2 pm:catch (pm:term $e1)))
   (if (is-member $head (quote (call reduce)))
       (let ($operand) $args (pm:dispatched $head $operand))
   (if (== $head forall)
       (let ($generator $check) $args (pm:for-all $generator $check))
   (if (is-member $head (quote (unique union intersection subtraction)))
       (pm:stream $head $args)
   (if (== $head case)
       (let ($key $rows) $args (pm:case-term (pm:term $key) $rows))
   (if (== $head progn) (pm:sequence $args)
   (if (== $head cut) pm:cut
   (if (== $head add-atom)
       (let ($space $atom) $args (pm:spaced $space pm:add (cons-atom $atom ())))
   (if (== $head remove-atom)
       (let ($space $atom) $args (pm:spaced $space pm:remove (cons-atom $atom ())))
   (if (== $head match)
       (let ($space $pattern $template) $args
         (pm:spaced $space pm:match (pm:list2 $pattern (pm:term $template))))
   (if (pm:atoms-form? $head $args)
       (let ($space) $args (pm:spaced $space pm:atoms ()))
   (if (== $head foldall)
       (let ($operation $generator $start) $args
         (pm:list2 pm:fold
           (pm:list3 pm:items (pm:term $operation)
             (pm:list3 pm:items (pm:term $start)
               (pm:list3 pm:items (pm:list2 pm:collapse (pm:term $generator)) pm:nil)))))
   (if (== $head maplist)
       (let ($operation $list) $args
         (pm:list2 pm:map
           (pm:list3 pm:items (pm:term $operation) (pm:list3 pm:items (pm:term $list) pm:nil))))
   (if (== $head prog1) (pm:first-of-all $args)
   (if (pm:goal? $head $args) (pm:goal-form $args)
   (if (pm:binds? $head $args) (pm:binder $head $args)
   (if (pm:defined $head) (pm:resolved-call $head $args)
   (if (is-member $head (pm:primitives))
       (pm:list3 pm:prim $head (pm:terms $args))
   (if (pm:written-value $head)
       (pm:list3 pm:data pm:tuple (pm:terms $e))
       (pm:list3 pm:host $head (pm:terms $args))))))))))))))))))))))))))))))))))))

; (translatePredicate (G A1 ... An)): a goal of the host's Prolog.  The goal's
; arguments are evaluated, by the machine; the host then calls the goal with
; the values as they are (pm:goal), and what the goal binds reaches the state.
(: pm:goal? (-> Atom Atom %Undefined%))
(= (pm:goal? $head $args)
   (if (== $head translatePredicate)
       (if (== (size-atom $args) 1)
           (let ($goal) $args
             (if (== (get-metatype $goal) Expression)
                 (if (== $goal ()) False
                   (not (== (get-metatype (car-atom $goal)) Variable)))
                 False))
           False)
       False))

(: pm:goal-form (-> Atom %Undefined%))
(= (pm:goal-form $args)
   (let ($goal) $args
     (let ($g $items) (decons-atom $goal)
       (pm:list3 pm:goal $g (pm:terms $items)))))

; map-atom, filter-atom and foldl-atom given a variable and a body, as in
; (map-atom (1 2 3) $x (+ $x 1)): the list (and foldl-atom's start) are
; evaluated by the machine; the variables and the body are the host's to bind
; and run, so they are handed over as written, and the host evaluates the
; application as code (pm:hostc), the machine's values quoted in it.
(: pm:binds? (-> Atom Atom %Undefined%))
(= (pm:binds? $head $args)
   (if (is-member $head (quote (map-atom filter-atom))) (== (size-atom $args) 3)
     (if (== $head foldl-atom) (== (size-atom $args) 5) False)))

(: pm:binder (-> Atom Atom %Undefined%))
(= (pm:binder $head $args)
   (let ($front $held) (pm:split-at (if (== $head foldl-atom) 2 1) $args)
     (pm:list3 pm:hostc $head (pm:items-onto-terms $front (pm:literals $held)))))

(= (pm:binder-call $f $items)
   (let $values (pm:list-of $items)
     (let ($front $held) (pm:split-at (if (== $f foldl-atom) 2 1) $values)
       (cons-atom $f (pm:appended (pm:quoted-each $front) $held)))))

(= (pm:quoted-each $values)
   (if (== $values ()) ()
     (let ($v $more) (decons-atom $values)
       (let $tail (pm:quoted-each $more) (cons-atom (pm:list2 quote $v) $tail)))))

(: pm:items-onto-terms (-> Atom %Undefined% %Undefined%))
(= (pm:items-onto-terms $es $items)
   (if (== $es ()) $items
     (let ($e $more) (decons-atom $es)
       (let $one (pm:term $e)
         (let $rest (pm:items-onto-terms $more $items) (pm:list3 pm:items $one $rest))))))

; A request on a space, (TAG SPACE FIELDS...).  A space written as an
; expression, such as a call that answers a space's name, is evaluated first.
(: pm:spaced (-> Atom %Undefined% %Undefined% %Undefined%))
(= (pm:spaced $space $tag $fields)
   (if (== (get-metatype $space) Expression)
       (let $s (pm:fresh-variable)
         (pm:list4 pm:let $s (pm:term $space) (cons-atom $tag (cons-atom $s $fields))))
       (cons-atom $tag (cons-atom $space $fields))))

; (call E) and (reduce E): the application E is made when it is reached, so a
; head that gained equations since this was compiled is called, also in a body
; that is running.  A written application whose head was neither a name of the
; program nor an operation of the host when compiled is (pm:late HEAD TERMS E):
; when reached, a head with equations is called on the arguments' values, and
; any other leaves the application as written, its arguments not evaluated.
; Of a written quotation, reduce evaluates what it holds and keeps the quote
; around the value, as both hosts do for written code ((reduce (quote (double
; 5))) is (quote 10)); call answers what the quotation holds, as CeTTa does
; (SWI-PeTTa raises there).  A variable: its value, an application, is
; taken apart and applied without evaluating the values again; a value that is
; no expression has no answer.  Anything else is evaluated as it stands.
(: pm:dispatched (-> %Undefined% Atom %Undefined%))
(= (pm:dispatched $form $operand)
   (if (== (get-metatype $operand) Variable) (pm:list2 pm:dispatch (pm:term $operand))
     (let $compiled (pm:term $operand)
       (if (pm:late-head? $operand $compiled)
           (let ($h $as) (decons-atom $operand)
             (pm:list4 pm:late $h (pm:terms $as) $operand))
           (if (and (== $form reduce) (pm:quotation? $operand))
               (pm:list3 pm:data quote (pm:terms (cdr-atom $operand)))
               $compiled)))))

(: pm:late-head? (-> Atom %Undefined% %Undefined%))
(= (pm:late-head? $operand $compiled)
   (if (== (get-metatype $compiled) Expression)
       (if (== (car-atom $compiled) pm:host)
           (let $head (car-atom $operand)
             (if (== (get-metatype $head) Symbol) True (not (pm:operation? $head))))
           False)
       False))

(: pm:quotation? (-> Atom %Undefined%))
(= (pm:quotation? $operand)
   (if (== (get-metatype $operand) Expression)
       (if (== (size-atom $operand) 2)
           (let $head (car-atom $operand)
             (if (== (get-metatype $head) Variable) False (== $head quote)))
           False)
       False))

(= (pm:application-of $value)
   (if (== (get-metatype $value) Expression)
       (if (== $value ()) pm:plain-value (pm:list2 pm:applied (pm:items-of $value)))
       pm:plain-value))

; (forall GENERATOR CHECK): whether CHECK, a value applied to each answer of
; GENERATOR, answers true every time.
(: pm:for-all (-> Atom Atom %Undefined%))
(= (pm:for-all $generator $check)
   (let* (($verdicts (pm:fresh-variable))
          ($each (pm:list2 pm:map
                   (pm:list3 pm:items (pm:term $check)
                     (pm:list3 pm:items (pm:list2 pm:collapse (pm:term $generator)) pm:nil))))
          ($none-false
            (pm:list3 pm:prim not
              (pm:list3 pm:items
                (pm:list3 pm:prim is-member
                  (pm:list3 pm:items (pm:list2 pm:lit false)
                    (pm:list3 pm:items (pm:list2 pm:lit $verdicts) pm:nil)))
                pm:nil))))
     (pm:list4 pm:let $verdicts $each $none-false)))

; (prog1 a b ...): each in turn, the first one's answer.
(: pm:first-of-all (-> Atom %Undefined%))
(= (pm:first-of-all $es)
   (let ($e $more) (decons-atom $es)
     (if (== $more ()) (pm:term $e)
       (let* (($kept (pm:fresh-variable))
              ($first (pm:term $e))
              ($rest (pm:then-all $more (pm:list2 pm:lit $kept))))
         (pm:list4 pm:let $kept $first $rest)))))

(: pm:then-all (-> Atom %Undefined% %Undefined%))
(= (pm:then-all $es $last)
   (if (== $es ()) $last
     (let ($e $more) (decons-atom $es)
       (let $first (pm:term $e)
         (let $rest (pm:then-all $more $last) (pm:list3 pm:seq $first $rest))))))

; A call of a name the program has equations for, by the number of arguments
; (the host decides the same way when it compiles a call): as many as some
; equation takes, a call; fewer than its equations take, a partial
; application, which is a value; more, the name applied to the values when the
; call runs (pm:invoke), which goes through an equation that answers a partial
; application and is an error otherwise.  A call of a name with declarations
; keeps its arguments as written beside their terms (pm:tcall): which are held
; and which are evaluated is the declaration's to say, when the call is made.
; A declaration describes calls with at most as many arguments as it has
; inputs, so the call inside an application to more is not typed.
(: pm:resolved-call (-> Atom Atom %Undefined%))
(= (pm:resolved-call $head $args)
   (let* (($count (size-atom $args))
          ($arities (pm:arities $head)))
     (if (is-member $count $arities)
         (if (pm:declared? $head)
             (pm:list4 pm:tcall $head (pm:terms $args) (pm:items-of $args))
             (pm:list3 pm:call $head (pm:terms $args)))
         (let $largest (pm:largest $arities)
           (if (< $count $largest)
               (pm:list3 pm:data partial
                 (pm:list3 pm:items (pm:list2 pm:lit $head)
                   (pm:list3 pm:items (pm:list3 pm:data pm:tuple (pm:terms $args)) pm:nil)))
               (pm:list2 pm:apply
                 (pm:list3 pm:items (pm:list2 pm:lit $head) (pm:terms $args))))))))

; The numbers of arguments a name's equations take: rows written when its
; equations are declared, before any of them is compiled, so that an equation
; can call its own name and names declared with it.
(= (pm:arities $name) (collapse (match &pm (pm:arity $name $count) $count)))

(= (pm:note-arity $name $count)
   (if (== (collapse (match &pm (pm:arity $name $count) known)) ())
       (let $row (pm:list3 pm:arity $name $count) (add-atom &pm $row))
       known))

(= (pm:items-length $items)
   (if (== $items pm:nil) 0
     (let ($_items $first $rest) $items (+ 1 (pm:items-length $rest)))))

(= (pm:largest $numbers)
   (let ($first $more) (decons-atom $numbers)
     (if (== $more ()) $first
       (let $other (pm:largest $more) (if (< $first $other) $other $first)))))

(: pm:split-at (-> %Undefined% Atom %Undefined%))
(= (pm:split-at $count $items)
   (if (== $count 0) (pm:list2 () $items)
     (let ($item $more) (decons-atom $items)
       (let ($front $rest) (pm:split-at (- $count 1) $more)
         (pm:list2 (cons-atom $item $front) $rest)))))

; A declaration (: NAME (-> T1 ... Tn T)) types NAME's calls.  A name's
; declarations are one row, (pm:decls NAME CHAINS), each chain the list
; (T1 ... Tn T), in the order given; a chain that is a renaming of one already
; there is not kept again.  Declarations are given before the equations that
; call the name are loaded, as the host's reader takes a file's declarations
; before its equations.
(: pm:type! (-> Atom %Undefined%))
(= (pm:type! $declaration)
   (let ($_colon $name $type) $declaration
     (if (and (is-member (get-metatype $name) (quote (Symbol Grounded)))
              (== (get-metatype $type) Expression))
         (if (== $type ()) undeclared
           (if (== (car-atom $type) ->) (pm:note-chain $name (cdr-atom $type)) undeclared))
         undeclared)))

(= (pm:note-chain $name $chain)
   (let $known (pm:chains-of $name)
     (if (pm:has-variant $chain $known) known
       (let* (($_gone (pm:clear-matching &pm (pm:decls $name $any)))
              ($row (pm:list3 pm:decls $name (pm:appended $known (cons-atom $chain ())))))
         (add-atom &pm $row)))))

; A name's chains, each a copy with type variables of its own.
(= (pm:chains-of $name)
   (let $rows (collapse (match &pm (pm:decls $name $chains) $chains))
     (if (== $rows ()) () (car-atom $rows))))

(= (pm:has-variant $chain $chains)
   (if (== $chains ()) False
     (let ($first $more) (decons-atom $chains)
       (if (=alpha $chain $first) True (pm:has-variant $chain $more)))))

(= (pm:declared? $name) (not (== (pm:chains-of $name) ())))

; The declarations that describe a call with COUNT arguments: those with at
; least as many inputs.  Each is (pm:sig DEMANDS TYPES DEMAND TYPE): for the
; inputs, what each asks of its argument and its type; then the same for the
; result.  What a type asks is read off the declaration as written, before a
; check binds any of its variables: a type variable that a check later binds
; to %Undefined% still checks.
(= (pm:chains-for $name $count) (pm:items-of (pm:long-enough (pm:chains-of $name) $count)))

(= (pm:long-enough $chains $count)
   (if (== $chains ()) ()
     (let ($chain $more) (decons-atom $chains)
       (let $tail (pm:long-enough $more $count)
         (if (> (size-atom $chain) $count)
             (let $sig (pm:signature $chain) (cons-atom $sig $tail))
             $tail)))))

(= (pm:signature $chain)
   (let* (($out (pm:last-of $chain))
          ($inputs (pm:but-last $chain)))
     (cons-atom pm:sig
       (pm:list4 (pm:demands $inputs) (pm:items-of $inputs) (pm:type-demand $out) $out))))

(= (pm:but-last $list)
   (let ($first $more) (decons-atom $list)
     (if (== $more ()) ()
       (let $tail (pm:but-last $more) (cons-atom $first $tail)))))

(= (pm:demands $types)
   (if (== $types ()) pm:nil
     (let ($type $more) (decons-atom $types)
       (pm:list3 pm:items (pm:type-demand $type) (pm:demands $more)))))

; What a declared type asks of its argument.  Atom: held as written.
; %Undefined% and _: evaluated, nothing checked.  Any other type, a type
; variable too: evaluated, and the value checked against the type.
(= (pm:type-demand $type)
   (if (== (get-metatype $type) Variable) pm:checked
     (if (== $type Atom) pm:held
       (if (or (== $type %Undefined%) (== $type _)) pm:free pm:checked))))

(= (pm:last-of $list)
   (let ($first $more) (decons-atom $list) (if (== $more ()) $first (pm:last-of $more))))

; An equation's body is answered as written when a declaration with as many
; inputs as the equation has arguments answers Atom.
(= (pm:body-held? $name $arity) (pm:atom-out (pm:chains-of $name) $arity))

(= (pm:atom-out $chains $arity)
   (if (== $chains ()) False
     (let ($chain $more) (decons-atom $chains)
       (if (== (size-atom $chain) (+ $arity 1))
           (if (== (pm:type-demand (pm:last-of $chain)) pm:held) True (pm:atom-out $more $arity))
           (pm:atom-out $more $arity)))))

; A number or a string at the head of a list: the list is data.  (get-type
; is a relation a program can extend, so its answers are collected: the
; question has one answer whatever the program added.)
(= (pm:written-value $head)
   (if (== (get-metatype $head) Grounded)
       (let $types (collapse (get-type $head))
         (or (is-member Number $types) (is-member String $types)))
       False))

; (let* ((P E) ...) B): the lets, nested.
(: pm:lets (-> Atom Atom %Undefined%))
(= (pm:lets $pairs $body)
   (if (== $pairs ()) (pm:term $body)
     (let ($pair $more) (decons-atom $pairs)
       (let ($p $value) $pair
         (let $first (pm:term $value)
           (let $rest (pm:lets $more $body) (pm:let-term $p $first $rest)))))))

; (superpose LIST): a written list is its elements, each an alternative, also
; when it is headed by a function's name; a variable is evaluated and the
; items of its value are the alternatives.
(: pm:choices (-> Atom %Undefined%))
(= (pm:choices $list)
   (if (== (get-metatype $list) Variable)
       (pm:list2 pm:each (pm:term $list))
       (pm:list2 pm:choice (pm:terms $list))))

; unique, union, intersection, subtraction: the answers of each argument are
; collected, the operation on lists is applied, and its items are the answers.
(: pm:stream (-> Atom Atom %Undefined%))
(= (pm:stream $head $args)
   (let $operation (if (== $head unique) unique-atom
                     (if (== $head union) union-atom
                       (if (== $head intersection) intersection-atom subtraction-atom)))
     (pm:list2 pm:each (pm:list3 pm:prim $operation (pm:collected-each $args)))))

(: pm:collected-each (-> Atom %Undefined%))
(= (pm:collected-each $es)
   (if (== $es ()) pm:nil
     (let ($e $more) (decons-atom $es)
       (let $one (pm:list2 pm:collapse (pm:term $e))
         (let $rest (pm:collected-each $more) (pm:list3 pm:items $one $rest))))))

; The rows of a case, (pm:rows-of ROWS CALLS).  A row is (pm:row PATTERN BODY),
; or, when its pattern has list views, (pm:row-v PATTERN CODE): the views, and
; inside them (pm:commit BODY); the row is taken only if its views open, and
; the next row is offered otherwise.  CALLS are the calls written in the rows'
; patterns: they run before the rows are offered, each row's pattern holding
; the variable its call answers into.
(: pm:rows (-> Atom %Undefined%))
(= (pm:rows $rows)
   (if (== $rows ()) (pm:list3 pm:rows-of pm:nil ())
     (let ($row $more) (decons-atom $rows)
       (let ($pattern $body) $row
         (let* (((pm:plain $pattern2 $views) (pm:plain-pattern pm:head $pattern))
                ($code (pm:term $body))
                ($lists (pm:views-of pm:v $views))
                ($calls (pm:views-of pm:c $views))
                ($one (if (== $lists ()) (pm:list3 pm:row $pattern2 $code)
                        (pm:list3 pm:row-v $pattern2 (pm:wrapped $lists (pm:list2 pm:commit $code)))))
                ((pm:rows-of $rest $later) (pm:rows $more)))
           (pm:list3 pm:rows-of (pm:list3 pm:items $one $rest) (pm:appended $calls $later)))))))

; The calls of a case's rows, in order, around the code that offers the rows.
(= (pm:hoisted $calls $code)
   (if (== $calls ()) $code
     (let ($call $more) (decons-atom $calls)
       (let (pm:c $l $term) $call
         (let $inner (pm:hoisted $more $code) (pm:list4 pm:let $l $term $inner))))))

; A case with a row (Empty BODY) keeps that row apart: it is taken when the key
; has no value at all.
(: pm:split-rows (-> Atom %Undefined%))
(= (pm:split-rows $rows)
   (if (== $rows ()) (pm:split () pm:none)
     (let ($row $more) (decons-atom $rows)
       (let (pm:split $plain $empty) (pm:split-rows $more)
         (let ($pattern $body) $row
           (if (== $pattern Empty)
               (pm:split $plain (pm:list2 pm:some $body))
               (pm:split (cons-atom $row $plain) $empty)))))))

(: pm:case-term (-> %Undefined% Atom %Undefined%))
(= (pm:case-term $key $rows)
   (let (pm:split $plain $empty) (pm:split-rows $rows)
     (let (pm:rows-of $items $calls) (pm:rows $plain)
       (if (== $empty pm:none)
           (if (== $calls ()) (pm:list3 pm:case $key $items)
             ; the key first, then the rows' calls, then the rows
             (let $value (pm:fresh-variable)
               (pm:list4 pm:let $value $key
                 (pm:hoisted $calls (pm:list3 pm:case (pm:list2 pm:lit $value) $items)))))
           (let (pm:some $body) $empty
             (let $otherwise (pm:term $body)
               (pm:hoisted $calls (pm:list4 pm:case-e $key $items $otherwise))))))))

; (progn a b ...): each in turn, the last one's answers.
(: pm:sequence (-> Atom %Undefined%))
(= (pm:sequence $es)
   (let ($e $more) (decons-atom $es)
     (if (== $more ()) (pm:term $e)
       (let $first (pm:term $e)
         (let $rest (pm:sequence $more) (pm:list3 pm:seq $first $rest))))))

;; ---- loading a program -----------------------------------------------------------

(: pm:program (-> Atom %Undefined%))
(= (pm:program $equations)
   (let* (($own (pm:without-host-relations $equations))
          ($new (pm:new-names $own))
          ($_names (pm:declare $own))
          ($_defined (pm:define $own))
          ($_users (pm:refresh-users-each $new)))
     defined))

; The names among these equations that are no functions of the program yet.
(: pm:new-names (-> Atom %Undefined%))
(= (pm:new-names $equations)
   (if (== $equations ()) ()
     (let ($equation $more) (decons-atom $equations)
       (let ($_eq $head $_body) $equation
         (let* (($name (car-atom $head)) ($tail (pm:new-names $more)))
           (if (or (pm:defined $name) (is-member $name $tail)) $tail (cons-atom $name $tail)))))))

; A name's equations as written, one row (pm:sources NAME EQUATIONS), in order.
; The clause rows are compiled from them: when a name gains its first equation
; or loses its last, the bodies that mention it are compiled again, so that an
; occurrence compiled as data becomes a call, or a call becomes data again.  A
; body that is running holds the term it was entered with and keeps it.
(= (pm:sources-of $name)
   (let $rows (collapse (match &pm (pm:sources $name $equations) $equations))
     (if (== $rows ()) () (car-atom $rows))))

(= (pm:set-sources $name $equations)
   (let $_gone (pm:clear-matching &pm (pm:sources $name $any))
     (if (== $equations ()) cleared
       (let $row (pm:list3 pm:sources $name $equations) (add-atom &pm $row)))))

(= (pm:rebuild $name)
   (let* (($_cleared (pm:clear-matching &pm (pm:clause $name $i $h $b)))
          ($_heads (pm:clear-matching &pm (pm:heads $name $i2 $h2)))
          ($_arities (pm:clear-matching &pm (pm:arity $name $count)))
          ($sources (pm:sources-of $name)))
     (if (== $sources ())
         (pm:clear-matching &pm (pm:fun $name))
         (let $_declared (pm:declare $sources) (pm:compile-each $sources)))))

(= (pm:refresh-users-each $names)
   (if (== $names ()) refreshed
     (let ($name $more) (decons-atom $names)
       (let $_one (pm:refresh-users $name) (pm:refresh-users-each $more)))))

(= (pm:refresh-users $name)
   (let $users (collapse (match &pm (pm:sources $g $equations)
                           (if (pm:mentions $name $equations) $g (empty))))
     (pm:rebuild-each $name $users)))

(= (pm:rebuild-each $name $users)
   (if (== $users ()) refreshed
     (let ($g $more) (decons-atom $users)
       (let $_one (if (== $g $name) same (pm:rebuild $g)) (pm:rebuild-each $name $more)))))

(= (pm:mentions $name $term)
   (let $kind (get-metatype $term)
     (if (== $kind Expression) (pm:mentions-any $name $term)
       (if (== $kind Variable) False (== $term $name)))))

(= (pm:mentions-any $name $items)
   (if (== $items ()) False
     (let ($first $rest) (decons-atom $items)
       (if (pm:mentions $name $first) True (pm:mentions-any $name $rest)))))

; get-type is the host's relation, and the checks of a typed call ask the host.
; An equation a program writes for it extends the relation there; it is no
; clause of the machine.  An equation is a clause when its head is an
; application of a name: one headed by a variable, by an expression or by a
; lone symbol defines no function and stays an atom of the space.
(: pm:without-host-relations (-> Atom %Undefined%))
(= (pm:without-host-relations $equations)
   (if (== $equations ()) ()
     (let ($equation $more) (decons-atom $equations)
       (let $tail (pm:without-host-relations $more)
         (let ($_eq $head $_body) $equation
           (if (pm:names-a-function $head) (cons-atom $equation $tail) $tail))))))

(: pm:names-a-function (-> Atom %Undefined%))
(= (pm:names-a-function $head)
   (if (== (get-metatype $head) Expression)
       (if (== $head ()) False
         (let $name (car-atom $head)
           (if (is-member (get-metatype $name) (quote (Symbol Grounded)))
               (not (== $name get-type))
               False)))
       False))

(: pm:declare (-> Atom %Undefined%))
(= (pm:declare $equations)
   (if (== $equations ()) declared
     (let ($equation $more) (decons-atom $equations)
       (let ($_eq $head $_body) $equation
         (let $name (car-atom $head)
           (let $_known (if (pm:defined $name) known
                          (let $fun (pm:list2 pm:fun $name) (add-atom &pm $fun)))
             (let $_arity (pm:note-arity $name (- (size-atom $head) 1))
               (pm:declare $more))))))))

(: pm:define (-> Atom %Undefined%))
(= (pm:define $equations)
   (if (== $equations ()) defined
     (let ($equation $more) (decons-atom $equations)
       (let ($_eq $head $_body) $equation
         (let* (($name (car-atom $head))
                ($kept (pm:appended (pm:sources-of $name) (cons-atom $equation ())))
                ($_sources (pm:set-sources $name $kept))
                ($_compiled (pm:compile-one $equation)))
           (pm:define $more))))))

(: pm:compile-each (-> Atom %Undefined%))
(= (pm:compile-each $equations)
   (if (== $equations ()) compiled
     (let ($equation $more) (decons-atom $equations)
       (let $_one (pm:compile-one $equation) (pm:compile-each $more)))))

; One equation as a clause row, after the name's rows so far.
(: pm:compile-one (-> Atom %Undefined%))
(= (pm:compile-one $equation)
   (let ($_eq $head $body) $equation
     (let* (($name (car-atom $head))
            ($index (size-atom (collapse (match &pm (pm:clause $name $i $h $b) $i))))
            ($written (cdr-atom $head))
            ($_constructors (pm:note-constructors $written))
            ((pm:plain $patterns $views) (pm:plain-each pm:head $written))
            ($pattern (pm:list2 pm:head (pm:items-of $patterns)))
            ($code (if (pm:body-held? $name (size-atom $written))
                       (pm:list2 pm:lit $body) (pm:term $body)))
            ($compiled (pm:with-views $views $code))
            ($clause (cons-atom pm:clause (pm:list4 $name $index $pattern $compiled)))
            ($_headed (add-atom &pm (pm:heads $name $index $pattern))))
       (add-atom &pm $clause))))

;; ---- how a rule is fired ----------------------------------------------------------
;
; A rule fires as (transform &pm SOURCES TEMPLATE &pm).  After (pm:lane! direct)
; the same rules are fired by the host's own operations: the first match of the
; sources in their order (a take and a read by match, a let by evaluating its
; expression, an absence by a match with no answer, a unique by a match with one
; answer, a collect by every answer of a match), then the takes removed and the
; instances added.  Every rule takes the machine's one state, so at most one
; match of a rule fires, and both ways fire the first.  In the code of a let, a
; variable the sources bind stands for a value: it is quoted before the code is
; evaluated, as transform quotes it.  A rule with another source form (a mode
; word or a space inside unique or collect, all, once) fires by transform on
; either lane.  (pm:lane! transform) is the default.
!(bind! &pm-lane (new-state transform))
(= (pm:lane! $lane) (let $_set (change-state! &pm-lane $lane) $lane))

; the parts of (, a b ...), or the one part
(= (pm:conjuncts $form)
   (if (== (get-metatype $form) Expression)
       (if (== $form ()) ()
           (if (== (car-atom $form) ,) (cdr-atom $form) (cons-atom $form ())))
       (cons-atom $form ())))

(= (pm:source-kind $c)
   (if (== (get-metatype $c) Expression)
       (if (== $c ()) other
         (let $head (car-atom $c)
           (if (== (get-metatype $head) Variable) other
             (if (== $head take) pm:c-take
             (if (== $head let) pm:c-let
             (if (== $head absent) pm:c-absent
             (if (== $head unique) (if (pm:plain-read (cdr-atom $c) 1) pm:c-unique other)
             (if (== $head collect) (if (pm:plain-read (cdr-atom $c) 2) pm:c-collect other)
             (if (is-member $head (read all once tf:as tf:let-first)) other
                 pm:c-read)))))))))
       other))

; a unique or a collect of a pattern read in the machine's own space
(= (pm:plain-read $parts $count)
   (if (== (size-atom $parts) $count)
       (let $inner (car-atom $parts)
         (if (== (get-metatype $inner) Expression)
             (if (== $inner ()) False
               (let $head (car-atom $inner)
                 (if (== (get-metatype $head) Variable) False
                   (not (is-member $head (, read take absent unique collect all once let tf:as tf:let-first))))))
             False))
       False))

(= (pm:plain-sources $conjuncts)
   (if (== $conjuncts ()) True
     (let ($c $rest) (decons-atom $conjuncts)
       (if (== (pm:source-kind $c) other) False (pm:plain-sources $rest)))))

; the variables the sources bind: those of a take's, a read's and an absence's
; pattern, and of a let's pattern
(= (pm:source-variables $conjuncts $found)
   (if (== $conjuncts ()) $found
     (let ($c $rest) (decons-atom $conjuncts)
       (pm:source-variables $rest
         (if (== (pm:source-kind $c) pm:c-let)
             (let ($_l $p $e) $c (pm:variables-of $p $found))
             (pm:variables-of $c $found))))))

(= (pm:variables-of $term $found)
   (let $kind (get-metatype $term)
     (if (== $kind Variable) (if (pm:variable-among $term $found) $found (cons-atom $term $found))
       (if (== $kind Expression) (pm:variables-of-each $term $found) $found))))

(= (pm:variables-of-each $terms $found)
   (if (== $terms ()) $found
     (let ($t $rest) (decons-atom $terms)
       (pm:variables-of-each $rest (pm:variables-of $t $found)))))

(= (pm:variable-among $v $variables)
   (if (== $variables ()) False
     (let ($w $rest) (decons-atom $variables)
       (if (== $w $v) True (pm:variable-among $v $rest)))))

; code with the sources' variables quoted
(= (pm:values-quoted $variables $code)
   (let $kind (get-metatype $code)
     (if (== $kind Variable)
         (if (pm:variable-among $code $variables) (cons-atom quote (cons-atom $code ())) $code)
       (if (== $kind Expression) (pm:values-quoted-each $variables $code) $code))))

(= (pm:values-quoted-each $variables $codes)
   (if (== $codes ()) ()
     (let ($c $rest) (decons-atom $codes)
       (let $first (pm:values-quoted $variables $c)
         (let $others (pm:values-quoted-each $variables $rest) (cons-atom $first $others))))))

; each source as (pm:c-take P), (pm:c-read P), (pm:c-absent P), (pm:c-unique P),
; (pm:c-collect P COLLECTION) or (pm:c-let P CODE)
(= (pm:direct-sources $variables $conjuncts)
   (if (== $conjuncts ()) ()
     (let ($c $rest) (decons-atom $conjuncts)
       (let* (($kind (pm:source-kind $c))
              ($one (if (== $kind pm:c-let)
                        (let ($_l $bound $code) $c
                          (pm:list3 pm:c-let $bound (pm:values-quoted $variables $code)))
                        (if (== $kind pm:c-read) (pm:list2 pm:c-read $c)
                        (if (== $kind pm:c-collect)
                            (let ($_c $each $collection) $c (pm:list3 pm:c-collect $each $collection))
                            (let ($_m $pattern) $c (pm:list2 $kind $pattern))))))
              ($others (pm:direct-sources $variables $rest)))
         (cons-atom $one $others)))))

; (pm:by-host SOURCES INSTANCES), or pm:by-transform
(= (pm:direct-form $sources $template)
   (let $conjuncts (pm:conjuncts $sources)
     (if (pm:plain-sources $conjuncts)
         (pm:list3 pm:by-host
                   (pm:direct-sources (pm:source-variables $conjuncts ()) $conjuncts)
                   (pm:conjuncts $template))
         pm:by-transform)))

; () when the rule does not fire
(: pm:fired (-> Atom Atom %Undefined%))
(= (pm:fired $sources $template)
   (if (== (get-state &pm-lane) direct)
       (pm:fired-as (pm:direct-form $sources $template) $sources $template)
       (collapse (transform &pm $sources $template &pm))))

(= (pm:fired-as $direct $sources $template)
   (if (== $direct pm:by-transform)
       (collapse (transform &pm $sources $template &pm))
       (let (pm:by-host $conjuncts $instances) $direct
         (let $solutions (collapse (once (pm:solved $conjuncts $instances)))
           (if (== $solutions ()) ()
             (let (pm:solution $taken $made) (car-atom $solutions)
               (let* (($_gone (pm:takes-gone $taken))
                      ($_made (pm:instances-made $made)))
                 (fired))))))))

(= (pm:solved $conjuncts $instances)
   (let $taken (pm:solve-all $conjuncts) (pm:list3 pm:solution $taken $instances)))

; the atoms the takes matched, in order
(= (pm:solve-all $conjuncts)
   (if (== $conjuncts ()) ()
     (let ($c $rest) (decons-atom $conjuncts)
       (let $took (pm:solve-one (car-atom $c) $c)
         (let $more (pm:solve-all $rest)
           (if (== $took pm:none) $more (cons-atom $took $more)))))))

; (Each branch names its own variables: SWI-PeTTa answers nothing for an `if`
; whose two branches bind one name by a let.)
(= (pm:solve-one $kind $c)
   (if (== $kind pm:c-take) (let ($_t $taken) $c (match &pm $taken $taken))
   (if (== $kind pm:c-let) (let ($_l $pattern $code) $c (let $pattern (eval $code) pm:none))
   (if (== $kind pm:c-absent)
       (let ($_a $none) $c (if (== (collapse (match &pm $none found)) ()) pm:none (empty)))
   (if (== $kind pm:c-unique)
       (let ($_u $only) $c
         (let $found (collapse (match &pm $only $only))
           (if (== (size-atom $found) 1) (let $only (car-atom $found) pm:none) (empty))))
   (if (== $kind pm:c-collect)
       (let ($_c $each $collection) $c
         (let $collection (collapse (match &pm $each $each)) pm:none))
       (let ($_r $read) $c (match &pm $read pm:none))))))))

; a removal answers once for each atom it removes, so it is collected
(= (pm:takes-gone $atoms)
   (if (== $atoms ()) done
     (let ($a $more) (decons-atom $atoms)
       (let $_gone (collapse (remove-atom &pm $a)) (pm:takes-gone $more)))))

(= (pm:instances-made $atoms)
   (if (== $atoms ()) done
     (let ($a $more) (decons-atom $atoms)
       (let $_made (add-atom &pm $a) (pm:instances-made $more)))))

;; ---- the rules -------------------------------------------------------------------

(: pm:rule! (-> Atom Atom Atom %Undefined%))
(= (pm:rule! $key $sources $template)
   (let $rule (cons-atom pm:rule (pm:list4 $key $sources $template
                                           (pm:direct-form $sources $template)))
     (add-atom &pm-rules $rule)))

; terms
!(pm:rule! (eval pm:lit)
    (take (pm:state (pm:eval (pm:lit $v)) $k $d $r))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (eval pm:if)
    (take (pm:state (pm:eval (pm:if $c $t $e)) $k $d $r))
    (pm:state (pm:eval $c) (pm:k (pm:k-if $t $e) $k) $d $r))
!(pm:rule! (eval pm:let)
    (take (pm:state (pm:eval (pm:let $p $e $b)) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-let $p $b) $k) $d $r))
; a choice: each item in turn, the later ones behind a choice point, the last
; with none
!(pm:rule! (eval pm:choice)
    (take (pm:state (pm:eval (pm:choice (pm:items $e pm:nil))) $k $d $r))
    (pm:state (pm:eval $e) $k $d $r))
!(pm:rule! (eval pm:choice)
    (take (pm:state (pm:eval (pm:choice (pm:items $e $more))) $k $d $r))
    (, (pm:state (pm:eval $e) $k (s $d) $r)
       (pm:alt (s $d) (pm:eval (pm:choice $more)) $k)))
!(pm:rule! (eval pm:choice)
    (take (pm:state (pm:eval (pm:choice pm:nil)) $k $d $r))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (eval pm:empty)
    (take (pm:state (pm:eval pm:empty) $k $d $r))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (eval pm:data)
    (take (pm:state (pm:eval (pm:data $f $args)) $k $d $r))
    (pm:state (pm:evlis $args pm:nil (pm:mk-data $f)) $k $d $r))
!(pm:rule! (eval pm:call)
    (take (pm:state (pm:eval (pm:call $f $args)) $k $d $r))
    (pm:state (pm:evlis $args pm:nil (pm:mk-call $f)) $k $d $r))
!(pm:rule! (eval pm:prim)
    (take (pm:state (pm:eval (pm:prim $f $args)) $k $d $r))
    (pm:state (pm:evlis $args pm:nil (pm:mk-prim $f)) $k $d $r))
; a call of a declared name: the declarations that describe a call with this
; many arguments, read when the call is made
!(pm:rule! (eval pm:tcall)
    (, (take (pm:state (pm:eval (pm:tcall $f $terms $written)) $k $d $r))
       (let $chains (pm:chains-for $f (pm:items-length $written))))
    (pm:state (pm:chains $f $terms $written $chains) $k $d $r))
!(pm:rule! (eval pm:add)
    (take (pm:state (pm:eval (pm:add $s $a)) $k $d $r))
    (pm:state (pm:effect (pm:add $s $a)) $k $d $r))
!(pm:rule! (eval pm:remove)
    (take (pm:state (pm:eval (pm:remove $s $a)) $k $d $r))
    (pm:state (pm:effect (pm:remove $s $a)) $k $d $r))
!(pm:rule! (eval pm:match)
    (take (pm:state (pm:eval (pm:match $s $p $t)) $k $d $r))
    (pm:state (pm:effect (pm:match $s $p $t)) $k $d $r))
!(pm:rule! (eval pm:atoms)
    (take (pm:state (pm:eval (pm:atoms $s)) $k $d $r))
    (pm:state (pm:effect (pm:atoms $s)) $k $d $r))
!(pm:rule! (eval pm:seq)
    (take (pm:state (pm:eval (pm:seq $a $b)) $k $d $r))
    (pm:state (pm:eval $a) (pm:k (pm:k-seq $b) $k) $d $r))
!(pm:rule! (eval pm:once)
    (take (pm:state (pm:eval (pm:once $e)) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-once $d) $k) $d $r))
; transaction: a choice point of its own, taken when the expression has no
; answer, and a journal of its own for the changes the expression makes
!(pm:rule! (eval pm:tx)
    (, (take (pm:state (pm:eval (pm:tx $e)) $k $d $r)) (take (pm:open-journals $open)))
    (, (pm:state (pm:eval $e) (pm:k (pm:k-tx $d) $k) (s $d) $r)
       (pm:alt (s $d) (pm:tx-failed) $k)
       (pm:open-journals (pm:items pm:nil $open))))
!(pm:rule! (eval pm:collapse)
    (take (pm:state (pm:eval (pm:collapse $e)) $k $d $r))
    (, (pm:state (pm:eval $e) (pm:k (pm:k-collect (s $d)) $k) (s $d) $r)
       (pm:alt (s $d) (pm:collected (s $d)) $k)
       (pm:bag (s $d) pm:nil)))
; catch: the expression under a frame that keeps the depth it was entered at
!(pm:rule! (eval pm:catch)
    (take (pm:state (pm:eval (pm:catch $e)) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-catch $d) $k) $d $r))
!(pm:rule! (eval pm:case)
    (take (pm:state (pm:eval (pm:case $e $rows)) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-case $rows) $k) $d $r))
!(pm:rule! (eval pm:case-e)
    (take (pm:state (pm:eval (pm:case-e $e $rows $empty)) $k $d $r))
    (, (pm:state (pm:eval $e) (pm:k (pm:k-case-e (s $d) $rows) $k) (s $d) $r)
       (pm:alt (s $d) (pm:empty-row (s $d) $empty) $k)
       (pm:unseen (s $d))))
!(pm:rule! (eval pm:cut)
    (take (pm:state (pm:eval pm:cut) $k $d $r))
    (pm:state (pm:cut-find $k) $k $d $r))
; a view: the value is the cell of the view's first item and its rest.  A list
; that is not empty opens into its first item and the others; an unknown becomes
; a cell, which a later view opens into the same parts; a cell whose rest is no
; list opens into its two parts; a symbol and the empty list have no view.
!(pm:rule! (eval pm:view)
    (, (take (pm:state (pm:eval (pm:view $l $h $t $b)) $k $d $r))
       (let $l (pm:cell $h $t)))
    (pm:state (pm:eval $b) $k $d $r))
!(pm:rule! (eval pm:view)
    (take (pm:state (pm:eval (pm:view $l $h $t $b)) $k $d $r))
    (pm:state pm:fail $k $d $r))
; a head that is no equation of the program: its arguments, then the host
!(pm:rule! (eval pm:host)
    (take (pm:state (pm:eval (pm:host $f $args)) $k $d $r))
    (pm:state (pm:evlis $args pm:nil (pm:mk-host $f)) $k $d $r))
; an operation of the host that binds variables in a body it is given
!(pm:rule! (eval pm:hostc)
    (take (pm:state (pm:eval (pm:hostc $f $args)) $k $d $r))
    (pm:state (pm:evlis $args pm:nil (pm:mk-hostc $f)) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-hostc $f)) $k $d $r))
       (let $call (pm:binder-call $f $acc)))
    (pm:state (pm:effect (pm:host-code $call)) $k $d $r))
; a goal of the host's Prolog: its arguments, then the host calls it
!(pm:rule! (eval pm:goal)
    (take (pm:state (pm:eval (pm:goal $g $args)) $k $d $r))
    (pm:state (pm:evlis $args pm:nil (pm:mk-goal $g)) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-goal $g)) $k $d $r))
       (let $values (pm:list-of $acc)))
    (pm:state (pm:effect (pm:host-goal $g $values)) $k $d $r))
; a value applied to values: head and arguments alike, then what the head is
!(pm:rule! (eval pm:apply)
    (take (pm:state (pm:eval (pm:apply $items)) $k $d $r))
    (pm:state (pm:evlis $items pm:nil (pm:mk-apply)) $k $d $r))
; foldall and maplist: the operation, a value, is applied on the machine: to
; each item and the answer so far (the item first, as the hosts do), or to
; each item
!(pm:rule! (eval pm:fold)
    (take (pm:state (pm:eval (pm:fold $items)) $k $d $r))
    (pm:state (pm:evlis $items pm:nil (pm:mk-fold)) $k $d $r))
!(pm:rule! (eval pm:map)
    (take (pm:state (pm:eval (pm:map $items)) $k $d $r))
    (pm:state (pm:evlis $items pm:nil (pm:mk-map)) $k $d $r))
; a computed list: its items are the alternatives
!(pm:rule! (eval pm:each)
    (take (pm:state (pm:eval (pm:each $e)) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-each) $k) $d $r))
; call and reduce of a variable: its value is an application, made as it is
!(pm:rule! (eval pm:dispatch)
    (take (pm:state (pm:eval (pm:dispatch $e)) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-dispatch) $k) $d $r))
!(pm:rule! (ret pm:k-dispatch)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-dispatch) $k) $d $r))
       (let (pm:applied $items) (pm:application-of $v)))
    (pm:state (pm:ready $items (pm:mk-apply)) $k $d $r))
!(pm:rule! (ret pm:k-dispatch)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-dispatch) $k) $d $r))
    (pm:state pm:fail $k $d $r))
; call and reduce of a written application whose head had no equations when
; compiled: with equations by now, a call; otherwise the application as written
!(pm:rule! (eval pm:late)
    (, (take (pm:state (pm:eval (pm:late $h $terms $written)) $k $d $r)) (pm:fun $h))
    (pm:state (pm:evlis $terms pm:nil (pm:mk-late $h)) $k $d $r))
!(pm:rule! (eval pm:late)
    (, (take (pm:state (pm:eval (pm:late $h $terms $written)) $k $d $r)) (absent (pm:fun $h)))
    (pm:state (pm:ret $written) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (take (pm:state (pm:ready $acc (pm:mk-late $h)) $k $d $r))
    (pm:state (pm:invoke $h $acc) $k $d $r))
; eval: the value of a variable is code.  An argument written as any other
; expression is evaluated as that expression, once: (eval (quote E)) is E as
; written, on both hosts.
!(pm:rule! (eval pm:reval)
    (take (pm:state (pm:eval (pm:reval $e)) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-reval) $k) $d $r))

; Arguments, left to right.  A literal is its value at once; another item is
; evaluated under a frame that keeps the rest.  The values are gathered newest
; first and put in order when the last is in: (pm:ready VALUES TARGET).
!(pm:rule! (ctl pm:evlis)
    (take (pm:state (pm:evlis (pm:items (pm:lit $v) $more) $done $target) $k $d $r))
    (pm:state (pm:evlis $more (pm:items $v $done) $target) $k $d $r))
!(pm:rule! (ctl pm:evlis)
    (take (pm:state (pm:evlis (pm:items $e $more) $done $target) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-evlis $more $done $target) $k) $d $r))
!(pm:rule! (ctl pm:evlis)
    (, (take (pm:state (pm:evlis pm:nil $done $target) $k $d $r))
       (let $values (pm:items-reversed $done pm:nil)))
    (pm:state (pm:ready $values $target) $k $d $r))
!(pm:rule! (ret pm:k-evlis)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-evlis $more $done $target) $k) $d $r))
    (pm:state (pm:evlis $more (pm:items $v $done) $target) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (take (pm:state (pm:ready $acc (pm:mk-call $f)) $k $d $r))
    (pm:state (pm:try $f $acc) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-data $f)) $k $d $r))
       (let $v (pm:expression $f $acc)))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-prim $f)) $k $d $r))
       (let closed (pm:open-items $acc))
       (let $v (pm:compute $f $acc)))
    (pm:state (pm:ret $v) $k $d $r))
; the operation raises: the error is raised on the machine
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-prim $f)) $k $d $r))
       (let closed (pm:open-items $acc))
       (let (pm:raised $error) (pm:raises $f $acc)))
    (pm:state (pm:raise $error) $k $d $r))
; no answer and nothing raised, as (index-atom (1 2 3) 5): no answer here
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-prim $f)) $k $d $r))
       (let closed (pm:open-items $acc))
       (let True (pm:answers-nothing $f $acc)))
    (pm:state pm:fail $k $d $r))
; applied to a value that holds a variable, the operation may answer more than
; once and bind as it answers (or of an unknown and True): a request, whose
; answers are alternatives
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-prim $f)) $k $d $r))
       (let open (pm:open-items $acc))
       (let $values (pm:list-of $acc)))
    (pm:state (pm:effect (pm:host $f $values)) $k $d $r))
; a head that was no name of the program when this was compiled: the host is
; asked.  When the name gains equations, the stored bodies that mention it are
; compiled again; this term, held by a body that is running, stays what it was
; compiled as, an expression.
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-host $f)) $k $d $r)) (pm:fun $f)
       (let $v (pm:expression $f $acc)))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-host $f)) $k $d $r)) (absent (pm:fun $f))
       (let $values (pm:list-of $acc)))
    (pm:state (pm:effect (pm:host $f $values)) $k $d $r))
; an applied value: a name with equations in the program is called; anything
; else (an operation of the host, a partial application, data) is the host's
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready (pm:items $f $args) (pm:mk-apply)) $k $d $r))
       (let call (pm:head-kind $f)))
    (pm:state (pm:invoke $f $args) $k $d $r))
; a partial application of a name of the program: its values, then these
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready (pm:items (partial $g $bound) $args) (pm:mk-apply)) $k $d $r))
       (let closure (pm:head-kind (partial $g $bound)))
       (let $all (pm:items-onto $bound $args)))
    (pm:state (pm:invoke $g $all) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready (pm:items $f (pm:items $start (pm:items $list pm:nil)))
                                (pm:mk-fold)) $k $d $r))
       (let $todo (pm:items-of $list)))
    (pm:state (pm:folding $f $todo $start) $k $d $r))
!(pm:rule! (ctl pm:folding)
    (take (pm:state (pm:folding $f pm:nil $acc) $k $d $r))
    (pm:state (pm:ret $acc) $k $d $r))
!(pm:rule! (ctl pm:folding)
    (take (pm:state (pm:folding $f (pm:items $x $more) $acc) $k $d $r))
    (pm:state (pm:ready (pm:items $f (pm:items $x (pm:items $acc pm:nil))) (pm:mk-apply))
              (pm:k (pm:k-fold $f $more) $k) $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready (pm:items $f (pm:items $list pm:nil)) (pm:mk-map)) $k $d $r))
       (let $todo (pm:items-of $list)))
    (pm:state (pm:mapping $f $todo pm:nil) $k $d $r))
!(pm:rule! (ctl pm:mapping)
    (, (take (pm:state (pm:mapping $f pm:nil $done) $k $d $r))
       (let $list (pm:reversed $done ())))
    (pm:state (pm:ret $list) $k $d $r))
!(pm:rule! (ctl pm:mapping)
    (take (pm:state (pm:mapping $f (pm:items $x $more) $done) $k $d $r))
    (pm:state (pm:ready (pm:items $f (pm:items $x pm:nil)) (pm:mk-apply))
              (pm:k (pm:k-map $f $more $done) $k) $d $r))
; a lambda: a fresh copy of it, its parameters met with the values, its body
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready (pm:items $f $args) (pm:mk-apply)) $k $d $r))
       (let lambda (pm:head-kind $f))
       (let $term (pm:lambda-applied $f $args)))
    (pm:state (pm:eval $term) $k $d $r))
; a name of the program applied to values: by their number, as at compile time
; An equation of exactly this many arguments is a row (pm:arity NAME COUNT): the
; rules that call read it, and the rules for any other number of values state
; its absence.  So "no equation takes this many" is a condition of those two
; rules and of no other, and an equation added later overturns it.
!(pm:rule! (ctl pm:invoke)
    (, (take (pm:state (pm:invoke $g $items) $k $d $r))
       (let $count (pm:items-length $items))
       (pm:arity $g $count)
       (let call (pm:exact-kind $g $items $count)))
    (pm:state (pm:try $g $items) $k $d $r))
; (a declared name: its declarations check the values; an argument of type
; Atom is the value it was given, which was evaluated before the name was known)
!(pm:rule! (ctl pm:invoke)
    (, (take (pm:state (pm:invoke $g $items) $k $d $r))
       (let $count (pm:items-length $items))
       (pm:arity $g $count)
       (let (typed $terms $written $chains) (pm:exact-kind $g $items $count)))
    (pm:state (pm:chains $g $terms $written $chains) $k $d $r))
!(pm:rule! (ctl pm:invoke)
    (, (take (pm:state (pm:invoke $g $items) $k $d $r))
       (let $count (pm:items-length $items))
       (absent (pm:arity $g $count))
       (let partial (pm:other-kind $g $items $count))
       (let $v (pm:partial-value $g $items)))
    (pm:state (pm:ret $v) $k $d $r))
; more values than any equation takes.  The hosts give an equation whose
; body is a partial application the arguments that application still lacks, and
; call it; a call no equation takes is an error.  The machine has no table of
; the host's operations, so it asks: the equations with the most arguments are
; called on the first values, and an answer that is a partial application
; takes the remaining values.  Any other answer is dropped.  When no answer was
; a partial application the call raises the hosts' error.  (An equation whose
; body is a whole call of a host operation is run before that is known, which
; the hosts do not do.)
!(pm:rule! (ctl pm:invoke)
    (, (take (pm:state (pm:invoke $g $items) $k $d $r))
       (let $count (pm:items-length $items))
       (absent (pm:arity $g $count))
       (let (over $front $rest $error) (pm:other-kind $g $items $count)))
    (, (pm:state (pm:try $g $front) (pm:k (pm:k-rest (s $d) $rest) $k) (s $d) $r)
       (pm:alt (s $d) (pm:over-end $error) $k)))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready (pm:items $f $args) (pm:mk-apply)) $k $d $r))
       (let host (pm:head-kind $f))
       (let $values (pm:list-of $args)))
    (pm:state (pm:effect (pm:host-applied $f $values)) $k $d $r))

; A typed call.  No declaration describes it: an ordinary call.  Otherwise each
; declaration is an alternative that evaluates the arguments for itself: an
; argument of type Atom is the argument as written; of type %Undefined% or _,
; its value; of any other type, its value checked against the type; which of
; the three, the declaration says as it was written (pm:sig).  The check
; asks the host whether the value has the type, and when the value has no type
; at all, whether that is its metatype; each way it holds is an alternative,
; and what it binds in the type reaches the types still to come.  The result is
; checked the same way against the last type, unless that is Atom, %Undefined%
; or _.
!(pm:rule! (ctl pm:chains)
    (take (pm:state (pm:chains $f $terms $written pm:nil) $k $d $r))
    (pm:state (pm:evlis $terms pm:nil (pm:mk-call $f)) $k $d $r))
!(pm:rule! (ctl pm:chains)
    (take (pm:state (pm:chains $f $terms $written (pm:items (pm:sig $ds $ts $od $ot) pm:nil)) $k $d $r))
    (pm:state (pm:targs $terms $written $ds $ts pm:nil (pm:mk-tcall $f $od $ot)) $k $d $r))
!(pm:rule! (ctl pm:chains)
    (take (pm:state (pm:chains $f $terms $written (pm:items (pm:sig $ds $ts $od $ot) $more)) $k $d $r))
    (, (pm:state (pm:targs $terms $written $ds $ts pm:nil (pm:mk-tcall $f $od $ot)) $k (s $d) $r)
       (pm:alt (s $d) (pm:chains $f $terms $written $more) $k)))
!(pm:rule! (ctl pm:targs)
    (, (take (pm:state (pm:targs pm:nil $ws $ds $ts $done $target) $k $d $r))
       (let $values (pm:items-reversed $done pm:nil)))
    (pm:state (pm:ready $values $target) $k $d $r))
!(pm:rule! (ctl pm:targs)
    (take (pm:state (pm:targs (pm:items $t $more) (pm:items $w $ws) (pm:items pm:held $ds)
                              (pm:items $type $ts) $done $target) $k $d $r))
    (pm:state (pm:targs $more $ws $ds $ts (pm:items $w $done) $target) $k $d $r))
!(pm:rule! (ctl pm:targs)
    (take (pm:state (pm:targs (pm:items $t $more) (pm:items $w $ws) (pm:items pm:free $ds)
                              (pm:items $type $ts) $done $target) $k $d $r))
    (pm:state (pm:eval $t) (pm:k (pm:k-targ $more $ws $ds $ts $done $target) $k) $d $r))
!(pm:rule! (ctl pm:targs)
    (take (pm:state (pm:targs (pm:items $t $more) (pm:items $w $ws) (pm:items pm:checked $ds)
                              (pm:items $type $ts) $done $target) $k $d $r))
    (pm:state (pm:eval $t)
              (pm:k (pm:k-check $type) (pm:k (pm:k-targ $more $ws $ds $ts $done $target) $k)) $d $r))
!(pm:rule! (ret pm:k-targ)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-targ $more $ws $ds $ts $done $target) $k) $d $r))
    (pm:state (pm:targs $more $ws $ds $ts (pm:items $v $done) $target) $k $d $r))
!(pm:rule! (ret pm:k-check)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-check $type) $k) $d $r))
    (pm:state (pm:effect (pm:guard $v $type)) (pm:k (pm:k-keep $v) $k) $d $r))
!(pm:rule! (ret pm:k-keep)
    (take (pm:state (pm:ret $u) (pm:k (pm:k-keep $v) $k) $d $r))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (take (pm:state (pm:ready $acc (pm:mk-tcall $f pm:checked $out)) $k $d $r))
    (pm:state (pm:try $f $acc) (pm:k (pm:k-check $out) $k) $d $r))
!(pm:rule! (ctl pm:ready)
    (take (pm:state (pm:ready $acc (pm:mk-tcall $f $kind $out)) $k $d $r))
    (pm:state (pm:try $f $acc) $k $d $r))

; A call: the clauses whose heads meet the arguments, in their order.  One: its
; body, with no choice point.  None: no answer.  Several: each in turn, the
; later ones behind a choice point, the last with none.  The clause's body runs
; under a frame that keeps the depth the call was made at.  When the caller's
; own frame is on top, the call is the last thing its caller does: the new
; frame takes that frame's place, so a loop written as a call runs in one frame.
!(pm:rule! (ctl pm:try)
    (, (take (pm:state (pm:try $f $args) (pm:k (pm:k-call $caller) $k) $d $r))
       (unique (pm:heads $f $i (pm:head $args)))
       (pm:clause $f $i (pm:head $args) $body))
    (pm:state (pm:eval $body) (pm:k (pm:k-call $d) $k) $d $r))
!(pm:rule! (ctl pm:try)
    (, (take (pm:state (pm:try $f $args) $k $d $r))
       (unique (pm:heads $f $i (pm:head $args)))
       (pm:clause $f $i (pm:head $args) $body))
    (pm:state (pm:eval $body) (pm:k (pm:k-call $d) $k) $d $r))
!(pm:rule! (ctl pm:try)
    (, (take (pm:state (pm:try $f $args) $k $d $r))
       (collect (pm:heads $f $i (pm:head $args)) $heads)
       (let $indices (pm:indices-in-order $heads)))
    (pm:state (pm:try-each $f $args $indices) $k $d $r))
!(pm:rule! (ctl pm:try-each)
    (take (pm:state (pm:try-each $f $args pm:nil) $k $d $r))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (ctl pm:try-each)
    (take (pm:state (pm:try-each $f $args (pm:items $i pm:nil)) $k $d $r))
    (pm:state (pm:enter $f $args $i $d) $k $d $r))
!(pm:rule! (ctl pm:try-each)
    (take (pm:state (pm:try-each $f $args (pm:items $i (pm:items $j $more))) $k $d $r))
    (, (pm:state (pm:enter $f $args $i $d) $k (s $d) $r)
       (pm:alt (s $d) (pm:try-each $f $args (pm:items $j $more)) $k)))
!(pm:rule! (ctl pm:enter)
    (, (take (pm:state (pm:enter $f $args $i $d0) (pm:k (pm:k-call $caller) $k) $d $r))
       (pm:clause $f $i (pm:head $args) $body))
    (pm:state (pm:eval $body) (pm:k (pm:k-call $d0) $k) $d $r))
!(pm:rule! (ctl pm:enter)
    (, (take (pm:state (pm:enter $f $args $i $d0) $k $d $r))
       (pm:clause $f $i (pm:head $args) $body))
    (pm:state (pm:eval $body) (pm:k (pm:k-call $d0) $k) $d $r))
!(pm:rule! (ctl pm:enter)
    (, (take (pm:state (pm:enter $f $args $i $d0) $k $d $r))
       (absent (pm:clause $f $i (pm:head $args) $b)))
    (pm:state pm:fail $k $d $r))

; Toward a value.  A call written as a let's pattern must answer the let's
; value, and the hosts hand the value to the call before it runs: a constructor
; in a body meets the value first and its items run toward the value's parts,
; so (S (plus $x $y)) toward (S (S Z)) runs (plus $x $y) toward (S Z), and the
; search for a function run backwards ends where the value does.  This is done
; for the constructors the program is known to take apart (pm:con); any other
; head runs, and its answer is met with the value.
!(pm:rule! (eval pm:toward)
    (take (pm:state (pm:eval (pm:toward $t $v $b)) $k $d $r))
    (pm:state (pm:eval-to $t $v) (pm:k (pm:k-seq $b) $k) $d $r))
; a constructor: the value's parts are what its items must answer
!(pm:rule! (ctl pm:eval-to)
    (, (take (pm:state (pm:eval-to (pm:data $f $items) $v) $k $d $r))
       (let (pm:pairs $pairs) (pm:shape-of $f $items $v)))
    (pm:state (pm:each-to $pairs $v) $k $d $r))
!(pm:rule! (ctl pm:eval-to)
    (, (take (pm:state (pm:eval-to (pm:host $f $items) $v) $k $d $r))
       (absent (pm:fun $f))
       (let (pm:pairs $pairs) (pm:shape-of $f $items $v)))
    (pm:state (pm:each-to $pairs $v) $k $d $r))
!(pm:rule! (ctl pm:eval-to)
    (, (take (pm:state (pm:eval-to (pm:data $f $items) $v) $k $d $r))
       (let pm:other (pm:shape-of $f $items $v)))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (ctl pm:eval-to)
    (, (take (pm:state (pm:eval-to (pm:host $f $items) $v) $k $d $r))
       (absent (pm:fun $f))
       (let pm:other (pm:shape-of $f $items $v)))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (ctl pm:each-to)
    (take (pm:state (pm:each-to (pm:items (pm:to $t $part) $more) $v) $k $d $r))
    (pm:state (pm:eval-to $t $part) (pm:k (pm:k-each-to $more $v) $k) $d $r))
!(pm:rule! (ctl pm:each-to)
    (take (pm:state (pm:each-to pm:nil $v) $k $d $r))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ret pm:k-each-to)
    (take (pm:state (pm:ret $u) (pm:k (pm:k-each-to $more $v) $k) $d $r))
    (pm:state (pm:each-to $more $v) $k $d $r))
; a call: its arguments, then each clause's body toward the value
!(pm:rule! (ctl pm:eval-to)
    (take (pm:state (pm:eval-to (pm:call $f $args) $v) $k $d $r))
    (pm:state (pm:evlis $args pm:nil (pm:mk-call-to $f $v)) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (take (pm:state (pm:ready $acc (pm:mk-call-to $f $v)) $k $d $r))
    (pm:state (pm:try-to $f $acc $v) $k $d $r))
!(pm:rule! (ctl pm:try-to)
    (, (take (pm:state (pm:try-to $f $args $v) (pm:k (pm:k-call $caller) $k) $d $r))
       (unique (pm:heads $f $i (pm:head $args)))
       (pm:clause $f $i (pm:head $args) $body))
    (pm:state (pm:eval-to $body $v) (pm:k (pm:k-call $d) $k) $d $r))
!(pm:rule! (ctl pm:try-to)
    (, (take (pm:state (pm:try-to $f $args $v) $k $d $r))
       (unique (pm:heads $f $i (pm:head $args)))
       (pm:clause $f $i (pm:head $args) $body))
    (pm:state (pm:eval-to $body $v) (pm:k (pm:k-call $d) $k) $d $r))
!(pm:rule! (ctl pm:try-to)
    (, (take (pm:state (pm:try-to $f $args $v) $k $d $r))
       (collect (pm:heads $f $i (pm:head $args)) $heads)
       (let $indices (pm:indices-in-order $heads)))
    (pm:state (pm:try-each-to $f $args $indices $v) $k $d $r))
!(pm:rule! (ctl pm:try-each-to)
    (take (pm:state (pm:try-each-to $f $args pm:nil $v) $k $d $r))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (ctl pm:try-each-to)
    (take (pm:state (pm:try-each-to $f $args (pm:items $i pm:nil) $v) $k $d $r))
    (pm:state (pm:enter-to $f $args $i $v $d) $k $d $r))
!(pm:rule! (ctl pm:try-each-to)
    (take (pm:state (pm:try-each-to $f $args (pm:items $i (pm:items $j $more)) $v) $k $d $r))
    (, (pm:state (pm:enter-to $f $args $i $v $d) $k (s $d) $r)
       (pm:alt (s $d) (pm:try-each-to $f $args (pm:items $j $more) $v) $k)))
!(pm:rule! (ctl pm:enter-to)
    (, (take (pm:state (pm:enter-to $f $args $i $v $d0) (pm:k (pm:k-call $caller) $k) $d $r))
       (pm:clause $f $i (pm:head $args) $body))
    (pm:state (pm:eval-to $body $v) (pm:k (pm:k-call $d0) $k) $d $r))
!(pm:rule! (ctl pm:enter-to)
    (, (take (pm:state (pm:enter-to $f $args $i $v $d0) $k $d $r))
       (pm:clause $f $i (pm:head $args) $body))
    (pm:state (pm:eval-to $body $v) (pm:k (pm:k-call $d0) $k) $d $r))
!(pm:rule! (ctl pm:enter-to)
    (, (take (pm:state (pm:enter-to $f $args $i $v $d0) $k $d $r))
       (absent (pm:clause $f $i (pm:head $args) $b)))
    (pm:state pm:fail $k $d $r))
; if, let and a list view hand the value on to the part that answers
!(pm:rule! (ctl pm:eval-to)
    (take (pm:state (pm:eval-to (pm:if $c $t $e) $v) $k $d $r))
    (pm:state (pm:eval $c) (pm:k (pm:k-if-to $t $e $v) $k) $d $r))
!(pm:rule! (ret pm:k-if-to)
    (, (take (pm:state (pm:ret $c) (pm:k (pm:k-if-to $t $e $v) $k) $d $r))
       (let $branch (pm:branch-for $c $t $e)))
    (pm:state (pm:eval-to $branch $v) $k $d $r))
!(pm:rule! (ctl pm:eval-to)
    (take (pm:state (pm:eval-to (pm:let $p $e $b) $v) $k $d $r))
    (pm:state (pm:eval $e) (pm:k (pm:k-let-to $p $b $v) $k) $d $r))
!(pm:rule! (ret pm:k-let-to)
    (take (pm:state (pm:ret $u) (pm:k (pm:k-let-to $u $b $v) $k) $d $r))
    (pm:state (pm:eval-to $b $v) $k $d $r))
!(pm:rule! (ret pm:k-let-to)
    (, (take (pm:state (pm:ret $u) (pm:k (pm:k-let-to $p $b $v) $k) $d $r))
       (absent (pm:state (pm:ret $u2) (pm:k (pm:k-let-to $u2 $b2 $v2) $k2) $d2 $r2)))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (ctl pm:eval-to)
    (, (take (pm:state (pm:eval-to (pm:view $l $h $t $b) $v) $k $d $r))
       (let $l (pm:cell $h $t)))
    (pm:state (pm:eval-to $b $v) $k $d $r))
!(pm:rule! (ctl pm:eval-to)
    (take (pm:state (pm:eval-to (pm:view $l $h $t $b) $v) $k $d $r))
    (pm:state pm:fail $k $d $r))
; a call of a pattern first, toward its own value; then the code toward this one
!(pm:rule! (ctl pm:eval-to)
    (take (pm:state (pm:eval-to (pm:toward $t $u $b) $v) $k $d $r))
    (pm:state (pm:eval-to $t $u) (pm:k (pm:k-then-to $b $v) $k) $d $r))
!(pm:rule! (ret pm:k-then-to)
    (take (pm:state (pm:ret $u) (pm:k (pm:k-then-to $b $v) $k) $d $r))
    (pm:state (pm:eval-to $b $v) $k $d $r))
; an operation of the host: its arguments, then the host is handed the value
; with the application, as it hands a let's value to the call in its pattern;
; (append ($x) $y) toward (1 2 3) has one answer that way.  With every argument
; known, an operation a rule computes is computed and its answer met.
!(pm:rule! (ctl pm:eval-to)
    (, (take (pm:state (pm:eval-to (pm:host $f $items) $v) $k $d $r))
       (absent (pm:fun $f)))
    (pm:state (pm:evlis $items pm:nil (pm:mk-host-to $f $v)) $k $d $r))
!(pm:rule! (ctl pm:eval-to)
    (take (pm:state (pm:eval-to (pm:prim $f $items) $v) $k $d $r))
    (pm:state (pm:evlis $items pm:nil (pm:mk-prim-to $f $v)) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-prim-to $f $v)) $k $d $r))
       (let closed (pm:open-items $acc))
       (let $u (pm:compute $f $acc)))
    (pm:state (pm:ret $u) (pm:k (pm:k-meet $v) $k) $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-prim-to $f $v)) $k $d $r))
       (let open (pm:open-items $acc))
       (let $values (pm:list-of $acc)))
    (pm:state (pm:effect (pm:host-to $f $values $v)) $k $d $r))
!(pm:rule! (ctl pm:ready)
    (, (take (pm:state (pm:ready $acc (pm:mk-host-to $f $v)) $k $d $r))
       (let $values (pm:list-of $acc)))
    (pm:state (pm:effect (pm:host-to $f $values $v)) $k $d $r))
; a literal and every other expression: it runs, and its answer must be the value
!(pm:rule! (ctl pm:eval-to)
    (take (pm:state (pm:eval-to $t $v) $k $d $r))
    (pm:state (pm:eval $t) (pm:k (pm:k-meet $v) $k) $d $r))
!(pm:rule! (ret pm:k-meet)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-meet $v) $k) $d $r))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ret pm:k-meet)
    (, (take (pm:state (pm:ret $u) (pm:k (pm:k-meet $v) $k) $d $r))
       (absent (pm:state (pm:ret $u2) (pm:k (pm:k-meet $u2) $k2) $d2 $r2)))
    (pm:state pm:fail $k $d $r))

; the answers of a request, in order: each a choice point made before its
; instance of the request is met.  The state holds the request, which shares its
; variables with the continuation; the rule's pattern names the request and the
; answer's instance with one variable, so taking the state binds, for this
; alternative alone, what the answer bound.
!(pm:rule! (ctl pm:answers-of)
    (take (pm:state (pm:answers-of $q (pm:items $first $more)) $k $d $r))
    (, (pm:state (pm:answer-of $q $first) $k (s $d) $r)
       (pm:alt (s $d) (pm:answers-of $q $more) $k)))
!(pm:rule! (ctl pm:answers-of)
    (take (pm:state (pm:answers-of $q pm:nil) $k $d $r))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (ctl pm:answer-of)
    (take (pm:state (pm:answer-of $q (pm:answered $v $q)) $k $d $r))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ctl pm:answer-of)
    (take (pm:state (pm:answer-of $q (pm:matched $t $q)) $k $d $r))
    (pm:state (pm:eval $t) $k $d $r))

; returns
; if takes its first branch when the condition's value is true and the other
; branch for every other value: false, any other atom, an unknown.
!(pm:rule! (ret pm:k-if)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-if $t $e) $k) $d $r))
       (let $branch (pm:branch-for $v $t $e)))
    (pm:state (pm:eval $branch) $k $d $r))
!(pm:rule! (ret pm:k-let)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-let $v $b) $k) $d $r))
    (pm:state (pm:eval $b) $k $d $r))
!(pm:rule! (ret pm:k-let)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-let $p $b) $k) $d $r))
       (absent (pm:state (pm:ret $u) (pm:k (pm:k-let $u $b2) $k2) $d2 $r2)))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (ret pm:halt)
    (, (take (pm:state (pm:ret $v) pm:halt $d $r)) (take (pm:given $answers)))
    (, (pm:state pm:fail pm:halt $d $r) (pm:given (pm:items $v $answers))))
!(pm:rule! (ret pm:k-seq)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-seq $b) $k) $d $r))
    (pm:state (pm:eval $b) $k $d $r))
!(pm:rule! (ret pm:k-call)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-call $d0) $k) $d $r))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ret pm:k-fold)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-fold $f $more) $k) $d $r))
    (pm:state (pm:folding $f $more $v) $k $d $r))
!(pm:rule! (ret pm:k-map)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-map $f $more $done) $k) $d $r))
    (pm:state (pm:mapping $f $more (pm:items $v $done)) $k $d $r))
; an answer to a call with more values than its equations take: a partial
; application takes the remaining values, and the call's own choice point no
; longer holds the error; any other answer is dropped
!(pm:rule! (ret pm:k-rest)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-rest $id $rest) $k) $d $r))
       (let True (pm:is-partial $v))
       (take (pm:alt $id (pm:over-end $error) $k2)))
    (, (pm:state (pm:ready (pm:items $v $rest) (pm:mk-apply)) $k $d $r)
       (pm:alt $id (pm:over-seen) $k2)))
!(pm:rule! (ret pm:k-rest)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-rest $id $rest) $k) $d $r))
       (let True (pm:is-partial $v)))
    (pm:state (pm:ready (pm:items $v $rest) (pm:mk-apply)) $k $d $r))
!(pm:rule! (ret pm:k-rest)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-rest $id $rest) $k) $d $r))
    (pm:state pm:fail $k $d $r))
; no answer is left: the error, when none was a partial application
!(pm:rule! (ctl pm:over-end)
    (take (pm:state (pm:over-end $error) $k $d $r))
    (pm:state (pm:raise $error) $k $d $r))
!(pm:rule! (ctl pm:over-seen)
    (take (pm:state (pm:over-seen) $k $d $r))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (ret pm:k-each)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-each) $k) $d $r))
       (let $items (pm:literals $v)))
    (pm:state (pm:eval (pm:choice $items)) $k $d $r))
!(pm:rule! (ret pm:k-reval)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-reval) $k) $d $r))
       (let $t (pm:compiled $v)))
    (pm:state (pm:eval $t) $k $d $r))

; An error.  The nearest catch frame names the depth its expression was entered
; at: the choice points made above it are taken, the newest first, and the error
; is the catch's answer.  With no catch frame the machine stops, holding the
; error (pm:uncaught ERROR).
!(pm:rule! (ret pm:k-catch)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-catch $d0) $k) $d $r))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ctl pm:raise)
    (take (pm:state (pm:raise $error) $k $d $r))
    (pm:state (pm:raise-find $error $k) $k $d $r))
!(pm:rule! (ctl pm:raise-find)
    (take (pm:state (pm:raise-find $error (pm:k (pm:k-catch $d0) $rest)) $k $d $r))
    (pm:state (pm:raise-to $d0 $error) $rest $d $r))
; (an error leaving a transaction: the transaction's changes are undone first)
!(pm:rule! (ctl pm:raise-find)
    (take (pm:state (pm:raise-find $error (pm:k (pm:k-tx $d0) $rest)) $k $d $r))
    (pm:state (pm:effect (pm:tx-undo (pm:raise-find $error $rest))) $k $d $r))
!(pm:rule! (ctl pm:raise-find)
    (take (pm:state (pm:raise-find $error (pm:k $frame $rest)) $k $d $r))
    (pm:state (pm:raise-find $error $rest) $k $d $r))
!(pm:rule! (ctl pm:raise-find)
    (, (take (pm:state (pm:raise-find $error (pm:top $id $link)) $k $d $r))
       (pm:below $id $link $frames))
    (pm:state (pm:raise-find $error $frames) $k $d $r))
!(pm:rule! (ctl pm:raise-find)
    (take (pm:state (pm:raise-find $error pm:halt) $k $d $r))
    (pm:state (pm:uncaught $error) pm:halt $d $r))
!(pm:rule! (ctl pm:raise-to)
    (take (pm:state (pm:raise-to $d $error) $k $d $r))
    (pm:state (pm:ret $error) $k $d $r))
!(pm:rule! (ctl pm:raise-to)
    (, (take (pm:state (pm:raise-to $d0 $error) $k (s $d) $r)) (take (pm:alt (s $d) $c $k2)))
    (pm:state (pm:raise-to $d0 $error) $k $d $r))

; once: at the depth it was entered at, the answer goes on; above it, the
; newest choice point is taken first
!(pm:rule! (ret pm:k-once)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-once $d) $k) $d $r))
    (pm:state (pm:ret $v) $k $d $r))
!(pm:rule! (ret pm:k-once)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-once $d0) $k) (s $d) $r))
       (take (pm:alt (s $d) $c $k2)))
    (pm:state (pm:ret $v) (pm:k (pm:k-once $d0) $k) $d $r))

; transaction: the first answer, as once.  At the depth of its own choice
; point the answer goes on, and its changes are handed to the transaction
; around it; above that depth the newest choice point is taken first.  With no
; answer its own choice point is reached, and the changes are undone.
!(pm:rule! (ret pm:k-tx)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-tx $d) $k) (s $d) $r))
       (take (pm:alt (s $d) (pm:tx-failed) $k2))
       (take (pm:open-journals (pm:items $changes $open)))
       (let $kept (pm:handed-on $changes $open)))
    (, (pm:state (pm:ret $v) $k $d $r) (pm:open-journals $kept)))
!(pm:rule! (ret pm:k-tx)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-tx $d0) $k) (s $d) $r))
       (take (pm:alt (s $d) $c $k2)))
    (pm:state (pm:ret $v) (pm:k (pm:k-tx $d0) $k) $d $r))
!(pm:rule! (ctl pm:tx-failed)
    (take (pm:state (pm:tx-failed) $k $d $r))
    (pm:state (pm:effect (pm:tx-undo pm:fail)) $k $d $r))

; collapse: an answer joins the bag and the search goes on; when nothing is
; left, the choice point made at entry returns the bag
!(pm:rule! (ret pm:k-collect)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-collect $id) $k) $d $r))
       (take (pm:bag $id $items)))
    (, (pm:state pm:fail $k $d $r) (pm:bag $id (pm:items $v $items))))
!(pm:rule! (ctl pm:collected)
    (, (take (pm:state (pm:collected $id) $k $d $r)) (take (pm:bag $id $items))
       (let $list (pm:reversed $items ())))
    (pm:state (pm:ret $list) $k $d $r))

; case: the value is offered to the rows in order; a row whose pattern meets it
; is taken, any other is passed over
!(pm:rule! (ret pm:k-case)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-case $rows) $k) $d $r))
    (pm:state (pm:select $v $rows) $k $d $r))
!(pm:rule! (ctl pm:select)
    (take (pm:state (pm:select $v (pm:items (pm:row $v $b) $more)) $k $d $r))
    (pm:state (pm:eval $b) $k $d $r))
; a row with list views: the rows after it wait in a choice point, which holds
; the value as it was; the row's pattern meets the value, its views open, and
; then the row is taken for good (the choice point goes)
!(pm:rule! (ctl pm:select)
    (take (pm:state (pm:select $v (pm:items (pm:row-v $p $code) $more)) $k $d $r))
    (, (pm:state (pm:meet-row $v $p $code) (pm:k (pm:k-row $d) $k) (s $d) $r)
       (pm:alt (s $d) (pm:select $v $more) $k)))
!(pm:rule! (ctl pm:meet-row)
    (take (pm:state (pm:meet-row $v $v $code) $k $d $r))
    (pm:state (pm:eval $code) $k $d $r))
!(pm:rule! (ctl pm:meet-row)
    (take (pm:state (pm:meet-row $v $p $code) $k $d $r))
    (pm:state pm:fail $k $d $r))
!(pm:rule! (eval pm:commit)
    (take (pm:state (pm:eval (pm:commit $b)) (pm:k (pm:k-row $d0) $k) $d $r))
    (pm:state (pm:cut-to $d0) (pm:k (pm:k-seq $b) $k) $d $r))
!(pm:rule! (ctl pm:select)
    (take (pm:state (pm:select $v (pm:items $row $more)) $k $d $r))
    (pm:state (pm:select $v $more) $k $d $r))
!(pm:rule! (ctl pm:select)
    (take (pm:state (pm:select $v pm:nil) $k $d $r))
    (pm:state pm:fail $k $d $r))

; case with an Empty row: the key's first value takes the mark made at entry;
; the choice point made at entry runs the Empty row only if the mark is still
; there, that is, if the key had no value
!(pm:rule! (ret pm:k-case-e)
    (, (take (pm:state (pm:ret $v) (pm:k (pm:k-case-e $id $rows) $k) $d $r))
       (take (pm:unseen $id)))
    (pm:state (pm:select $v $rows) $k $d $r))
!(pm:rule! (ret pm:k-case-e)
    (take (pm:state (pm:ret $v) (pm:k (pm:k-case-e $id $rows) $k) $d $r))
    (pm:state (pm:select $v $rows) $k $d $r))
!(pm:rule! (ctl pm:empty-row)
    (, (take (pm:state (pm:empty-row $id $body) $k $d $r)) (take (pm:unseen $id)))
    (pm:state (pm:eval $body) $k $d $r))
!(pm:rule! (ctl pm:empty-row)
    (take (pm:state (pm:empty-row $id $body) $k $d $r))
    (pm:state pm:fail $k $d $r))

; cut: the nearest clause frame names the depth its call was made at; the
; choice points above it are taken, the newest first.  An expression that runs
; as a goal of its own (once, collapse, catch, a transaction) keeps a cut
; inside it: the frame names the depth the cut reaches, which leaves the
; form's own choice point in place.
!(pm:rule! (ctl pm:cut-find)
    (take (pm:state (pm:cut-find (pm:k (pm:k-call $d0) $rest)) $k $d $r))
    (pm:state (pm:cut-to $d0) $k $d $r))
!(pm:rule! (ctl pm:cut-find)
    (take (pm:state (pm:cut-find (pm:k (pm:k-once $d0) $rest)) $k $d $r))
    (pm:state (pm:cut-to $d0) $k $d $r))
!(pm:rule! (ctl pm:cut-find)
    (take (pm:state (pm:cut-find (pm:k (pm:k-catch $d0) $rest)) $k $d $r))
    (pm:state (pm:cut-to $d0) $k $d $r))
!(pm:rule! (ctl pm:cut-find)
    (take (pm:state (pm:cut-find (pm:k (pm:k-collect $id) $rest)) $k $d $r))
    (pm:state (pm:cut-to $id) $k $d $r))
!(pm:rule! (ctl pm:cut-find)
    (take (pm:state (pm:cut-find (pm:k (pm:k-tx $d0) $rest)) $k $d $r))
    (pm:state (pm:cut-to (s $d0)) $k $d $r))
!(pm:rule! (ctl pm:cut-find)
    (take (pm:state (pm:cut-find (pm:k $frame $rest)) $k $d $r))
    (pm:state (pm:cut-find $rest) $k $d $r))
!(pm:rule! (ctl pm:cut-find)
    (, (take (pm:state (pm:cut-find (pm:top $id $link)) $k $d $r)) (pm:below $id $link $frames))
    (pm:state (pm:cut-find $frames) $k $d $r))
!(pm:rule! (ctl pm:cut-find)
    (take (pm:state (pm:cut-find pm:halt) $k $d $r))
    (pm:state (pm:ret true) $k $d $r))
!(pm:rule! (ctl pm:cut-to)
    (take (pm:state (pm:cut-to $d) $k $d $r))
    (pm:state (pm:ret true) $k $d $r))
!(pm:rule! (ctl pm:cut-to)
    (, (take (pm:state (pm:cut-to $d0) $k (s $d) $r)) (take (pm:alt (s $d) $c $k2)))
    (pm:state (pm:cut-to $d0) $k $d $r))

; The continuation in parts.  The state keeps the newest frames; when 12 or more
; are there, the driver's offer of this rule moves all but the newest 6 into a
; row of their own, (pm:below ID LINK FRAMES), and the kept frames end in
; (pm:top ID LINK).  LINK holds the variables the two parts share (the answers
; so far share none with the frames: they came from states that failed): the state
; goes on binding its copy, and when a value returns to (pm:top ID LINK) the
; row is read with the same LINK, so the frames come back with what was bound
; meanwhile.  The row stays: a choice point made above it names it too.
!(pm:rule! (spill)
    (, (take (pm:state $c (pm:k $f1 (pm:k $f2 (pm:k $f3 (pm:k $f4 (pm:k $f5 (pm:k $f6 (pm:k $g1 (pm:k $g2 (pm:k $g3 (pm:k $g4 (pm:k $g5 (pm:k $g6 $rest)))))))))))) $d $r))
       (let (pm:linked $id $link)
            (pm:link-of (pm:k $g1 (pm:k $g2 (pm:k $g3 (pm:k $g4 (pm:k $g5 (pm:k $g6 $rest)))))) (pm:above $c $f1 $f2 $f3 $f4 $f5 $f6))))
    (, (pm:state $c (pm:k $f1 (pm:k $f2 (pm:k $f3 (pm:k $f4 (pm:k $f5 (pm:k $f6 (pm:top $id $link))))))) $d $r)
       (pm:below $id $link (pm:k $g1 (pm:k $g2 (pm:k $g3 (pm:k $g4 (pm:k $g5 (pm:k $g6 $rest)))))))))
!(pm:rule! (ret pm:top)
    (, (take (pm:state (pm:ret $v) (pm:top $id $link) $d $r)) (pm:below $id $link $frames))
    (pm:state (pm:ret $v) $frames $d $r))

; no answer here: the newest choice point, or the end
!(pm:rule! (fail)
    (, (take (pm:state pm:fail $k (s $d) $r)) (take (pm:alt (s $d) $c $k2)))
    (pm:state $c $k2 $d $r))
!(pm:rule! (fail)
    (, (take (pm:state pm:fail $k z $r)) (take (pm:given $answers)))
    (pm:done $answers))

;; ---- what a rule computes ----------------------------------------------------------

(= (pm:expression $f $items)
   (if (== $f pm:tuple) (pm:list-of $items) (cons-atom $f (pm:list-of $items))))

; A constructor's items toward a value.  (pm:pairs ((pm:to ITEM PART) ...)): the
; value has the constructor's shape, each item beside the part it must answer.
; pm:other: the value has another shape, so the constructor cannot answer it.
; pm:open: the value is not known yet, or the head is not known to be a
; constructor, so the expression runs and its answer is met with the value.
; A constructor is known from the program: a symbol at the head of a pattern in
; the head of one of its equations (pm:con NAME), which the program takes apart
; and so builds.  The host's metatype does not tell: it is Symbol for the
; native operations of a library as well.
(= (pm:shape-of $f $items $v)
   (if (== (get-metatype $v) Variable) pm:open
     (let $sort (pm:head-sort $f)
       (if (== $sort operation) pm:open
         (if (== (get-metatype $v) Expression)
             (if (== $sort tuple) (pm:paired $items $v)
               (if (== $v ()) pm:other
                 (let $head (car-atom $v)
                   (if (== (get-metatype $head) Variable) pm:other
                     (if (== $head $f)
                         (let $parts (cdr-atom $v) (pm:paired $items $parts))
                         pm:other)))))
             pm:other)))))

(= (pm:head-sort $f)
   (if (== $f pm:tuple) tuple
     (if (== (collapse (match &pm (pm:con $f) known)) ()) operation constructor)))

; The constructors of an equation's head: each symbol at the head of a pattern
; that is no list view and no call of a function of the program.
(= (pm:note-constructors $patterns)
   (if (== $patterns ()) noted
     (let ($p $more) (decons-atom $patterns)
       (let $_one (pm:note-constructor $p) (pm:note-constructors $more)))))

(= (pm:note-constructor $p)
   (if (== (get-metatype $p) Expression)
       (if (or (== $p ()) (pm:call-pattern? $p)) noted
         (let ($head $args) (decons-atom $p)
           (let $_head (if (pm:is-view $p) noted
                         (if (== (get-metatype $head) Symbol)
                             (if (== (collapse (match &pm (pm:con $head) known)) ())
                                 (add-atom &pm (pm:con $head))
                                 noted)
                             noted))
             (pm:note-constructors $args))))
       noted))

(= (pm:paired $items $parts)
   (if (== (pm:items-length $items) (size-atom $parts))
       (let $pairs (pm:pair-up $items $parts) (pm:list2 pm:pairs $pairs))
       pm:other))

(= (pm:pair-up $items $parts)
   (if (== $items pm:nil) pm:nil
     (let ($_items $first $rest) $items
       (let ($part $others) (decons-atom $parts)
         (let $tail (pm:pair-up $rest $others)
           (pm:list3 pm:items (pm:list3 pm:to $first $part) $tail))))))

; Whether one of the values holds a variable: open, or closed.
(= (pm:open-items $items)
   (if (== $items pm:nil) closed
     (let ($_items $first $rest) $items
       (if (== (pm:open-value $first) open) open (pm:open-items $rest)))))

; (A value with no metatype is, on SWI-PeTTa, a list whose end is not known yet
; or a term the host holds that is no atom, such as the error term its catch
; answers.  The first opens as a cell, and is open when a part of it is; the
; second does not open and is taken as it is.)
(= (pm:open-value $value)
   (case (get-metatype $value)
     ((Variable open)
      (Expression (pm:open-list $value))
      ($other closed)
      (Empty (pm:open-cell $value)))))

(= (pm:open-cell $value)
   (let $found (collapse (let $value (cons-atom $h $t) (pm:pair $h $t)))
     (if (== $found ()) closed
       (let (pm:pair $first $rest) (car-atom $found)
         (if (== (pm:open-value $first) open) open (pm:open-value $rest))))))

; (The others of a list can be a variable: a list whose end is not known yet,
; such as the host's answer to (append (42 10) $B).)
(= (pm:open-list $list)
   (if (== (get-metatype $list) Variable) open
     (if (== $list ()) closed
       (let ($first $rest) (decons-atom $list)
         (if (== (pm:open-value $first) open) open (pm:open-list $rest))))))

; The operation applied to the values as they are: reduce dispatches on the
; head and hands the values over without evaluating them again.
; An operation that raises has no answer here (the rule that asks pm:raises
; then raises the error on the machine): nothing is raised inside a rule.
(= (pm:compute $op $items)
   (let $call (cons-atom $op (pm:list-of $items))
     (let (pm:value $v) (catch (let $r (reduce $call) (pm:list2 pm:value $r))) $v)))

; (pm:raised ERROR) when the operation raises for these values.  reduce, by
; which a rule computes, answers nothing where the operation raises, so the
; application is evaluated as code, the values quoted in it, under catch, which
; answers the error as a value.  (Asked only when reduce had no answer; the
; operations a rule computes change nothing, so asking again is harmless.)
(= (pm:raises $op $items)
   (let $call (cons-atom $op (pm:quoted-each (pm:list-of $items)))
     (let $caught (collapse (catch (eval $call)))
       (if (== $caught ()) pm:quiet
         (let $first (car-atom $caught)
           (if (pm:is-error $first) (pm:list2 pm:raised $first) pm:quiet))))))

; The branch an if takes for its condition's value.
(= (pm:branch-for $value $then $else)
   (if (== (get-metatype $value) Variable) $else
     (if (== $value true) $then $else)))

(= (pm:is-error $value)
   (if (== (get-metatype $value) Expression)
       (if (== $value ()) False
         (let $head (car-atom $value)
           (if (== (get-metatype $head) Variable) False (== $head Error))))
       False))

; Whether the operation has no answer for these values and raises nothing.
(= (pm:answers-nothing $op $items)
   (== (pm:raises $op $items) pm:quiet))

; Values as terms: each a literal.
(= (pm:literals $list)
   (if (== $list ()) pm:nil
     (let ($first $rest) (decons-atom $list)
       (let $tail (pm:literals $rest) (pm:list3 pm:items (pm:list2 pm:lit $first) $tail)))))

; A value as code.
(= (pm:compiled $value) (pm:term $value))

; What an applied value is: a name the program has equations for, or the host's.
; (A value the host holds that is no atom, such as a partial application of
; one of its own operations, has no metatype.)
; (A name the host knows as a function has the metatype Grounded there.)
(= (pm:head-kind $f)
   (case (get-metatype $f)
     ((Symbol (if (pm:defined $f) call host))
      (Grounded (if (pm:defined $f) call host))
      (Expression (pm:partial-kind $f))
      ($other host)
      (Empty host))))

; (partial NAME VALUES) with NAME a name of the program is the machine's own
; partial application.
(= (pm:partial-kind $f)
   (if (== (size-atom $f) 3)
       (if (== (car-atom $f) |->) lambda
       (if (== (car-atom $f) partial)
           (let $name (car-atom (cdr-atom $f))
             (if (is-member (get-metatype $name) (quote (Symbol Grounded)))
                 (if (pm:defined $name) closure host)
                 host))
           host))
       host))

; The cell of a first item and a rest, either of which may be unknown.
(= (pm:cell $first $rest) (cons-atom $first $rest))

; A copy of a term with variables of its own: a stored atom is read back that way.
(= (pm:fresh-copy $term)
   (let* (($_stored (add-atom &pm-copies (pm:copy $term)))
          ((pm:copy $fresh) (match &pm-copies (pm:copy $c) (pm:copy $c)))
          ($_gone (pm:clear-matching &pm-copies (pm:copy $any))))
     $fresh))

; (|-> PARAMETERS BODY) applied to values: the body under the parameters met
; with the values.  Each application has a copy of its own, so that applying
; the lambda again starts from unbound parameters; a variable the lambda shares
; with its surroundings and that is still unbound when it is applied is not
; shared by the copy.  (The lambda is taken apart by position: a lambda form
; written here would be named by the host when it compiles this library.)
(= (pm:lambda-applied $f $items)
   (let ($_arrow $parameters $body) (pm:fresh-copy $f)
     (let $values (pm:list-of $items)
       (if (== (get-metatype $parameters) Expression)
           (pm:lambda-by-count $parameters $body $values (size-atom $parameters) (size-atom $values))
           (pm:let-term $parameters (pm:list2 pm:lit $values) (pm:compiled $body))))))

; As many values as parameters: the body.  Fewer: a lambda of the parameters
; left, the first ones met with the values.  More: the body's answer applied to
; the values left.
(= (pm:lambda-by-count $parameters $body $values $wanted $given)
   (if (== $given $wanted)
       (pm:let-term $parameters (pm:list2 pm:lit $values) (pm:compiled $body))
     (if (< $given $wanted)
         (let ($front $rest) (pm:split-at $given $parameters)
           (let $smaller (cons-atom |-> (pm:list2 $rest $body))
             (pm:list4 pm:let $front (pm:list2 pm:lit $values) (pm:list2 pm:lit $smaller))))
         (let ($front $rest) (pm:split-at $wanted $values)
           (let $applied (pm:let-term $parameters (pm:list2 pm:lit $front) (pm:compiled $body))
             (pm:list2 pm:apply (pm:list3 pm:items $applied (pm:literals $rest))))))))

; A call of exactly as many values as an equation takes: plain, or checked by
; the declarations that describe it.
(= (pm:exact-kind $g $items $count)
   (let $chains (pm:chains-for $g $count)
     (if (== $chains pm:nil) call
       (pm:list4 typed (pm:literals (pm:list-of $items)) $items $chains))))

; A call of a number of values no equation takes: fewer than the most any
; equation takes is a partial application, more is the call of those equations
; on the first values.
(= (pm:other-kind $g $items $count)
   (let* (($arities (pm:arities $g))
          ($largest (pm:largest $arities)))
     (if (< $count $largest) partial
       (let (pm:split $front $rest) (pm:items-split $largest $items pm:nil)
         (pm:list4 over $front $rest (pm:over-application $g $arities $count))))))

; (pm:split FRONT REST): the first COUNT items, and the others.
(= (pm:items-split $count $items $front)
   (if (== $count 0) (pm:list3 pm:split (pm:items-reversed $front pm:nil) $items)
     (let ($_items $first $rest) $items
       (pm:items-split (- $count 1) $rest (pm:list3 pm:items $first $front)))))

; The hosts' error for a call with more values than any equation takes.
(= (pm:over-application $g $arities $count)
   (let $known (sort-atom (unique-atom $arities))
     (pm:list3 Error
       (pm:list3 domain_error (pm:list3 function_input_arities $g $known) $count)
       none)))

; Whether a value is a partial application: the machine's own, an expression
; (partial NAME VALUES), or the host's, which is no atom and has no metatype.
(= (pm:is-partial $v)
   (case (get-metatype $v)
     ((Expression (pm:partial-expression $v))
      ($other False)
      (Empty True))))

(= (pm:partial-expression $v)
   (if (== (size-atom $v) 3)
       (let $head (car-atom $v)
         (if (== (get-metatype $head) Variable) False (== $head partial)))
       False))

; (pm:linked ID LINK) for frames about to leave the state: ID is the number of
; rules fired so far, LINK the variables of the frames that the rest of the
; state holds too.
(= (pm:link-of $below $above)
   (let $shared (pm:variables-in-both (pm:variables-of $below ()) (pm:variables-of $above ()) ())
     (pm:list3 pm:linked (get-state &pm-count) (cons-atom pm:vars $shared))))

(= (pm:variables-in-both $candidates $others $found)
   (if (== $candidates ()) $found
     (let ($v $rest) (decons-atom $candidates)
       (pm:variables-in-both $rest $others
         (if (pm:variable-among $v $others) (cons-atom $v $found) $found)))))

(= (pm:items-reversed $items $onto)
   (if (== $items pm:nil) $onto
     (let ($_items $first $rest) $items
       (pm:items-reversed $rest (pm:list3 pm:items $first $onto)))))

(= (pm:items-onto $list $items)
   (if (== $list ()) $items
     (let ($first $rest) (decons-atom $list)
       (let $tail (pm:items-onto $rest $items) (pm:list3 pm:items $first $tail)))))

(= (pm:partial-value $g $items)
   (pm:list3 partial $g (pm:list-of $items)))

;; ---- the driver --------------------------------------------------------------------

; The key of a state: which rules can fire.
(= (pm:key $control $continuation)
   (if (== $control pm:fail) (fail)
     (let $tag (car-atom $control)
       (if (== $tag pm:eval)
           (let ($_e $term) $control
             (pm:list2 eval (if (== (get-metatype $term) Expression) (car-atom $term) $term)))
       (if (== $tag pm:ret)
           (pm:list2 ret (if (== $continuation pm:halt) pm:halt
                           (if (== (car-atom $continuation) pm:top) pm:top
                             (let ($_k $frame $_rest) $continuation (car-atom $frame)))))
           (pm:list2 ctl $tag))))))

; The rules of a key, each offered once, in table order.
(= (pm:fire $key)
   (let $rules (collapse (match &pm-rules (pm:rule $key $sources $template $direct)
                           (pm:offer $sources $template $direct)))
     (pm:offer-each $rules)))

(= (pm:offer-each $offers)
   (if (== $offers ()) 0
     (let ($offer $more) (decons-atom $offers)
       (let ($_offer $sources $template $direct) $offer
         (let $fired (if (== (get-state &pm-lane) direct)
                         (pm:fired-as $direct $sources $template)
                         (collapse (transform &pm $sources $template &pm)))
           (if (== $fired ()) (pm:offer-each $more) 1))))))

; A request, performed between rules.  An add or a remove answers the control the
; state goes on with.  An application by the host and a match answer
; (pm:alternatives ITEMS), each item an answer beside the instance of the request
; that gave it: (pm:answered VALUE REQUEST) or (pm:matched TERM REQUEST).  The
; answers are collected, and a collected answer is a copy with variables of its
; own; the instance says what the answer bound in the request.
(= (pm:perform $request)
   (let $tag (car-atom $request)
     (if (== $tag pm:add)
         (let ($_t $space $atom) $request
           (let* (($_done (add-atom $space $atom))
                  ($_noted (pm:note-added $request))
                  ($_clause (if (pm:own-equation $space $atom)
                                (let $one (cons-atom $atom ()) (pm:program $one))
                                skipped)))
             (pm:list2 pm:ret true)))
     (if (== $tag pm:remove)
         (let ($_t $space $atom) $request
           (let* (($observe (or (pm:in-transaction) (pm:equation-pattern? $space $atom)))
                  ($before (if $observe (pm:rows-meeting $space $atom) ()))
                  ($gone (pm:removed $space $atom))
                  ($removed (if $observe
                                (pm:without-each (pm:rows-meeting $space $atom) $before) ()))
                  ($_noted (pm:note-removed $space $removed))
                  ($_clauses (pm:unprogram-each $space $removed)))
             (pm:list2 pm:ret $gone)))
     (if (== $tag pm:tx-undo)
         ; a transaction with no answer, or one an error leaves: its changes are
         ; undone, the newest first; the machine goes on as the request says
         (let $_undone (pm:undo-innermost) (cons-atom pm:go-on ()))
     (if (== $tag pm:host)
         ; a written application of a host operation: the host applies the head
         ; to the values as they are, once; every answer is an alternative, data
         ; comes back as it went, and what the operation raises is raised
         (let $found (pm:asked $request) (pm:list2 pm:alternatives (pm:items-of $found)))
     (if (== $tag pm:host-applied)
         ; a head that came as a value: the hosts apply it with reduce, where an
         ; operation that raises has no answer
         (let ($_t $f $values) $request
           (let $call (cons-atom $f $values)
             (let $found (collapse (let $value (reduce $call) (pm:answered $value $request)))
               (pm:list2 pm:alternatives (pm:items-of $found)))))
     (if (== $tag pm:host-code)
         ; an application the host evaluates as code
         (let $found (pm:coded $request) (pm:list2 pm:alternatives (pm:items-of $found)))
     (if (== $tag pm:host-goal)
         ; a goal of the host's Prolog, called with the values as they are
         (let $found (pm:asked-goal $request) (pm:list2 pm:alternatives (pm:items-of $found)))
     (if (== $tag pm:guard)
         ; a value against a declared type: each way the value has the type;
         ; when the value has no type at all, its metatype
         (let $ways (pm:ways $request) (pm:list2 pm:alternatives (pm:items-of $ways)))
     (if (== $tag pm:host-to)
         ; the same, the host handed the value the application must answer
         (let $found (pm:asked-to $request) (pm:list2 pm:alternatives (pm:items-of $found)))
     (if (== $tag pm:atoms)
         ; the atoms of a space; of &self, the program's
         (let $found (pm:atoms-of $request) (pm:list2 pm:alternatives (pm:items-of $found)))
         (let ($_t $space $pattern $term) $request
           (let $found (collapse (match $space $pattern (pm:matched $term $request)))
             (pm:list2 pm:alternatives (pm:items-of (pm:shown $request $found)))))))))))))))))

; The ways a value has a declared type, each (pm:answered true REQUEST), the
; request as that way bound it.  (A function of its own: inside pm:perform's
; equation CeTTa's PeTTa lane evaluated code held in the value again.)
(= (pm:ways $request)
   (let ($_t $value $type) $request
     (let $exact (collapse (let $type (get-type $value) (pm:answered true $request)))
       (if (== $exact ())
           (collapse (let $type (get-metatype $value) (pm:answered true $request)))
           $exact))))

(= (pm:coded $request)
   (let ($_t $call) $request
     (collapse (let $value (eval $call) (pm:answered $value $request)))))

; A written application of a host operation, asked once.  The application is
; code the host evaluates, each value given by a variable of its own:
;   (let $x1 (quote V1) (let $x2 (quote V2) (F $x1 $x2)))
; so a value that is an expression is handed over as it is, and what the
; operation raises is raised (the driver raises it on the machine).  reduce
; cannot be the way: it answers nothing where the operation raises, and asking
; a second time would do twice what the operation does.
; This way serves the operand of once, which wants the first answer and no
; other: the host is asked for as much.  Every other written application is
; answered in turn (pm:in-turn).
(= (pm:asked $request)
   (let ($_t $f $values) $request
     (let $code (pm:application $f $values)
       (collapse (once (let $value (eval $code) (pm:answered $value $request)))))))

; The same, the host handed the value the application must answer.
(= (pm:asked-to $request)
   (let ($_t $f $values $v) $request
     (let $code (pm:application $f $values)
       (collapse (let $v (eval $code) (pm:answered $v $request))))))

; A goal of the host's Prolog, called once: (translatePredicate (G $x1 ... $xn))
; as code, each value given by a variable of its own.  Each way the goal holds
; is an answer; what it raises is raised.
(= (pm:asked-goal $request)
   (let ($_t $g $values) $request
     (let $variables (pm:fresh-each $values)
       (let $code (pm:handed $variables $values
                    (pm:list2 translatePredicate (cons-atom $g $variables)))
         (collapse (let $value (eval $code) (pm:answered $value $request)))))))

(= (pm:application $f $values)
   (let $variables (pm:fresh-each $values)
     (pm:handed $variables $values (cons-atom $f $variables))))

(= (pm:fresh-each $values)
   (if (== $values ()) ()
     (let ($_first $more) (decons-atom $values)
       (let $tail (pm:fresh-each $more)
         (let $x (pm:fresh-variable) (cons-atom $x $tail))))))

(= (pm:handed $variables $values $body)
   (if (== $variables ()) $body
     (let ($x $xs) (decons-atom $variables)
       (let ($v $vs) (decons-atom $values)
         (let $inner (pm:handed $xs $vs $body)
           (pm:list4 let $x (pm:list2 quote $v) $inner))))))

;; ---- the program's view of &self --------------------------------------------------

(= (pm:hide-loaded!)
   (let* (($_old (pm:clear-matching &pm-hidden (pm:own $any)))
          ($_all (collapse (let $atom (get-atoms &self)
                             (let $row (pm:list2 pm:own $atom) (add-atom &pm-hidden $row))))))
     hidden))

; What a match of the program finds in &self: the host's answers without those
; the machine's own atoms give, each of these taken out once.
(= (pm:shown $request $found)
   (let ($_t $space $pattern $term) $request
     (if (== $space &self)
         (let $own (collapse (match &pm-hidden (pm:own $pattern) (pm:matched $term $request)))
           (if (== $own ()) $found (pm:without-each $own $found)))
         $found)))

(= (pm:atoms-of $request)
   (let ($_t $space) $request
     (let $found (collapse (let $atom (get-atoms $space) (pm:answered $atom $request)))
       (if (== $space &self)
           (let $own (collapse (match &pm-hidden (pm:own $atom) (pm:answered $atom $request)))
             (pm:without-each $own $found))
           $found))))

(= (pm:without-each $gone $rows)
   (if (== $gone ()) $rows
     (let ($one $more) (decons-atom $gone)
       (pm:without-each $more (pm:without-one $one $rows)))))

; A removal the program asks for.  From &self, an atom the machine's own atoms
; do not unify with is removed by the host as it is.  One they do unify with
; (a pattern as wide as an equation or a declaration) is removed row by row:
; each row of the program it meets, unless the machine has an atom that row
; unifies with; a row that is an equation of the program stops being a clause
; row.  (The host is not asked about such an atom at all, not even on an empty
; space: on SWI-PeTTa removing an equation from any space removes the
; function, and this one may be the machine's.  What the host would raise for
; the atom, as SWI-PeTTa does for an equation whose function name is unknown,
; is therefore not raised.)  An atom that is an unbound variable is the host's
; to answer.
(= (pm:removed $space $atom)
   (if (== $space &self)
       (if (== (get-metatype $atom) Variable) (remove-atom $space $atom)
         (if (== (collapse (once (match &pm-hidden (pm:own $atom) met))) ())
             (remove-atom $space $atom)
             (pm:remove-each (pm:without-each (pm:hidden-meeting $atom)
                                              (pm:rows-meeting $space $atom)))))
       (remove-atom $space $atom)))

(= (pm:hidden-meeting $atom)
   (collapse (let $row (match &pm-hidden (pm:own $own) $own)
               (if (== (collapse (let $atom $row met)) ()) (empty) $row))))

(= (pm:remove-each $rows)
   (if (== $rows ()) false
     (let ($row $more) (decons-atom $rows)
       (let $rest (pm:remove-each $more)
         (if (== (collapse (once (match &pm-hidden (pm:own $row) met))) ())
             (let $_gone (remove-atom &self $row) true)
             $rest)))))

; Broad equation patterns also require updating the private clause store.
; The before/after occurrence difference owns that update exactly once.
(= (pm:equation-pattern? $space $atom)
   (if (== $space &self)
       (not (== (collapse (let $atom (quote (= $head $body)) met)) ()))
       False))

; An equation of the program's own space: added while the program runs it
; becomes a clause row as well, and removed, its clause rows go.
(= (pm:own-equation $space $atom)
   (if (== $space &self)
       (if (== (get-metatype $atom) Expression)
           (if (== (size-atom $atom) 3)
               (if (== (car-atom $atom) =)
                   (let $head (car-atom (cdr-atom $atom))
                     (if (== (get-metatype $head) Expression)
                         (if (== $head ()) False
                           (is-member (get-metatype (car-atom $head)) (quote (Symbol Grounded))))
                         False))
                   False)
               False)
           False)
       False))

(= (pm:unprogram $equation)
   (let ($_eq $head $_body) $equation
     (let* (($name (car-atom $head))
            ($kept (pm:without-one $equation (pm:sources-of $name)))
            ($_set (pm:set-sources $name $kept))
            ($_rebuilt (pm:rebuild $name)))
       (if (== $kept ()) (pm:refresh-users $name) kept))))

(= (pm:unprogram-each $space $rows)
   (if (== $rows ()) removed
     (let ($row $more) (decons-atom $rows)
       (let $_one (if (pm:own-equation $space $row) (pm:unprogram $row) skipped)
         (pm:unprogram-each $space $more)))))

;; ---- transactions ----------------------------------------------------------------
;
; (pm:open-journals JOURNALS): the transactions open now, the innermost first,
; each the changes made to spaces while it ran, the newest first:
; (pm:added SPACE ATOM) and (pm:removed SPACE ROWS), ROWS the atoms the
; removal took, measured before and after the host operation. Undoing an
; addition clears the matching rows and restores all but one added occurrence,
; so both one-occurrence and all-occurrence host removals keep the right bag.
; The rows a removal takes, and the rows an undone addition meets, are found
; by reading the whole space: a removal inside a transaction and an undone
; addition cost the size of the space.  Only the machine's own add-atom and
; remove-atom requests are noted; what a host operation changes is not.

(= (pm:in-transaction)
   (not (== (collapse (match &pm (pm:open-journals (pm:items $changes $outer)) open)) ())))

(= (pm:note-change $change)
   (let $found (collapse (match &pm (pm:open-journals (pm:items $changes $outer))
                           (pm:in $changes $outer)))
     (if (== $found ()) skipped
       (let (pm:in $changes $outer) (car-atom $found)
         (let* (($_gone (pm:clear-matching &pm (pm:open-journals $any)))
                ($noted (pm:list3 pm:items $change $changes))
                ($row (pm:list2 pm:open-journals (pm:list3 pm:items $noted $outer)))
                ($_put (add-atom &pm $row)))
           noted)))))

(= (pm:note-added $request)
   (let ($_t $space $atom) $request
     (pm:note-change (pm:list3 pm:added $space $atom))))

(= (pm:note-removed $space $rows)
   (if (pm:in-transaction)
       (pm:note-change (pm:list3 pm:removed $space $rows))
       skipped))

; The atoms of a space an atom unifies with, as they are stored.
(= (pm:rows-meeting $space $atom)
   (collapse (let $row (get-atoms $space)
               (if (== (collapse (let $atom $row met)) ()) (empty) $row))))

; A transaction's answer hands its changes to the transaction around it.
(= (pm:handed-on $changes $open)
   (if (== $open pm:nil) pm:nil
     (let ($_items $outer $rest) $open
       (pm:list3 pm:items (pm:items-reversed (pm:items-reversed $changes pm:nil) $outer) $rest))))

(= (pm:undo-innermost)
   (let $found (collapse (match &pm (pm:open-journals (pm:items $changes $outer))
                           (pm:in $changes $outer)))
     (if (== $found ()) undone
       (let (pm:in $changes $outer) (car-atom $found)
         (let* (($_gone (pm:clear-matching &pm (pm:open-journals $any)))
                ($row (pm:list2 pm:open-journals $outer))
                ($_put (add-atom &pm $row)))
           (pm:undo $changes))))))

(= (pm:undo $changes)
   (if (== $changes pm:nil) undone
     (let ($_items $change $older) $changes
       (let $_one (pm:undo-change $change) (pm:undo $older)))))

(= (pm:undo-change $change)
   (if (== (car-atom $change) pm:added)
       (let ($_t $space $atom) $change
         (let* (($rows (pm:rows-meeting $space $atom))
                ($_gone (pm:clear-matching $space $atom))
                ($others (pm:without-one $atom $rows))
                ($_back (pm:add-each $space $others))
                ($_clause (if (pm:own-equation $space $atom)
                              (let $_out (pm:unprogram-each $space $rows) (pm:program-own $space $others))
                              skipped)))
           undone))
       (let ($_t $space $rows) $change
         (let $_back (pm:add-each $space $rows) (pm:program-own $space $rows)))))

; The rows without the first that is the atom up to the names of its variables.
(= (pm:without-one $atom $rows)
   (if (== $rows ()) ()
     (let ($row $more) (decons-atom $rows)
       (if (=alpha $row $atom) $more
         (let $tail (pm:without-one $atom $more) (cons-atom $row $tail))))))

(= (pm:add-each $space $rows)
   (if (== $rows ()) added
     (let ($row $more) (decons-atom $rows)
       (let $_one (add-atom $space $row) (pm:add-each $space $more)))))

; The rows that are equations of the program are the machine's clauses again.
(= (pm:program-own $space $rows)
   (if (== $rows ()) programmed
     (let ($row $more) (decons-atom $rows)
       (let $_one (if (pm:own-equation $space $row)
                      (let $one (cons-atom $row ()) (pm:program $one))
                      skipped)
         (pm:program-own $space $more)))))

; A request performed, or the error it raised, to be raised on the machine.
(= (pm:performed $request)
   (let $next (catch (pm:perform $request))
     (if (pm:is-error $next) (pm:list2 pm:raise $next) $next)))

;; ---- a request answered in turn ---------------------------------------------------
;
; A written application of a host operation is asked once, and its answers are
; taken from the host one at a time: the host's own enumeration is the
; alternative.  The answer before the newest is given to the machine with the
; choice point (pm:more-of ID REQUEST) behind it, and the machine runs inside
; the host's enumeration until it comes back to that choice point (the host is
; then asked for the next answer) or has dropped it (a once, a cut, a caught
; error: the host is asked for no more).  The host is one answer ahead, so a
; request with one answer is delivered with no choice point and nothing runs
; inside its enumeration.  A relation of the host with no last answer is
; answered as far as the program asks.  What the host raises while it is asked
; for the next answer is raised when the machine comes back for it.

; What the continuation asks of a request: its first answer, when the request
; is the operand of once, whose frame is the next to be answered; every answer,
; in turn, otherwise.
(= (pm:demand-of $continuation)
   (if (== (get-metatype $continuation) Expression)
       (if (== (size-atom $continuation) 3)
           (let $frame (car-atom (cdr-atom $continuation))
             (if (== (get-metatype $frame) Expression)
                 (if (== $frame ()) all
                   (let $tag (car-atom $frame)
                     (if (== (get-metatype $tag) Variable) all
                       (if (== $tag pm:k-once) first all))))
                 all))
           all)
       all))

(= (pm:answered-in-turn $request $continuation)
   (if (== (car-atom $request) pm:host) (== (pm:demand-of $continuation) all) False))

(= (pm:next-turn)
   (let $id (+ (get-state &pm-turns) 1) (let $_set (change-state! &pm-turns $id) $id)))

; The answer held back for a request, taken: () or (ITEM).
(= (pm:held-back $id)
   (let $found (collapse (match &pm-copies (pm:pending $id $item) $item))
     (let $_gone (pm:clear-matching &pm-copies (pm:pending $id $any)) $found)))

(= (pm:hold-back $id $item)
   (let $row (pm:list3 pm:pending $id $item) (add-atom &pm-copies $row)))

(= (pm:in-turn $request)
   (let* (($id (pm:next-turn))
          ($_named (pm:fired
                     (take (pm:state (pm:effect $q) $k $d $r))
                     (pm:state (pm:more-of $id $q) $k $d $r)))
          ($code (let ($_t $f $values) $request (pm:application $f $values)))
          ($ended (catch (collapse (once (let $value (eval $code)
                                           (pm:turn $id (pm:answered $value $request))))))))
     (pm:turn-ended $id $ended)))

; One answer of the host.  The first is held back and the host asked for more.
; From the second on, the one held back is given with the choice point behind
; it and the machine runs: back at the choice point, the host is asked again
; (this alternative of the enumeration has no answer, so the host goes on).
(= (pm:turn $id $item)
   (let* (($before (pm:held-back $id))
          ($_kept (pm:hold-back $id $item)))
     (if (== $before ()) (empty)
       (let* (($given (car-atom $before))
              ($_fired (pm:fired
                         (take (pm:state (pm:more-of $id $q) $k $d $r))
                         (, (pm:state (pm:answer-of $q $given) $k (s $d) $r)
                            (pm:alt (s $d) (pm:more-of $id $q) $k))))
              ($outcome (pm:drive-in $id)))
         (if (== $outcome pm:more) (empty) $outcome)))))

; The enumeration is over: the host has no more answers (the one held back is
; the last, given with no choice point), or it raised (the one held back is
; given, and the error is raised behind it), or the machine went on without the
; rest.
(= (pm:turn-ended $id $ended)
   (let $last (pm:held-back $id)
     (if (pm:is-error $ended) (pm:after-last $id $last (pm:list2 pm:raise $ended))
       (if (== $ended ()) (pm:after-last $id $last pm:fail)
         (let $outcome (car-atom $ended) (if (== $outcome pm:gone) pm:stepped $outcome))))))

(= (pm:after-last $id $last $then)
   (let $fired
        (if (== $last ())
            (pm:fired
              (take (pm:state (pm:more-of $id $q) $k $d $r))
              (pm:state $then $k $d $r))
            (let $given (car-atom $last)
              (if (== $then pm:fail)
                  (pm:fired
                    (take (pm:state (pm:more-of $id $q) $k $d $r))
                    (pm:state (pm:answer-of $q $given) $k $d $r))
                  (pm:fired
                    (take (pm:state (pm:more-of $id $q) $k $d $r))
                    (, (pm:state (pm:answer-of $q $given) $k (s $d) $r)
                       (pm:alt (s $d) $then $k))))))
     (if (== $fired ()) pm:stuck pm:stepped)))

; The machine run inside a request's enumeration: until it is back at the
; request's choice point (pm:more), or has dropped it (pm:gone), or stops.
(= (pm:drive-in $id)
   (if (pm:spent) pm:out-of-fuel
     (let $outcome (pm:step)
       (if (== $outcome pm:stepped)
           (let $_n (change-state! &pm-count (+ (get-state &pm-count) 1))
             (if (pm:turn-live $id) (pm:drive-in $id) pm:gone))
           (if (== $outcome pm:stuck) (if (pm:waits-at $id) pm:more $outcome) $outcome)))))

(= (pm:waits-at $id)
   (not (== (collapse (match &pm (pm:state (pm:more-of $id $q) $k $d $r) waits)) ())))

(= (pm:turn-live $id)
   (if (== (collapse (match &pm (pm:alt $d (pm:more-of $id $q) $k) live)) ())
       (pm:waits-at $id)
       True))

; The driver has no limit of its own.  A caller may set one with (pm:limit! STEPS);
; a run that reaches it stops with pm:out-of-fuel, which is a stop and never an
; answer or the lack of one.  (pm:limit! none) removes it.
(= (pm:spent)
   (let $limit (get-state &pm-limit)
     (if (== $limit none) False (>= (get-state &pm-count) $limit))))

(= (pm:limit! $steps) (let $_set (change-state! &pm-limit $steps) $steps))

; (The machine has one state.  More than one means a rule's take left its
; state in place; the driver stops and says so.)
(= (pm:step)
   (if (pm:spilled) pm:stepped (pm:step-on)))

; Every sixth rule the frames below the newest are offered to a row of their own.
(= (pm:spilled)
   (if (== (% (get-state &pm-count) 6) 0) (== (pm:fire (spill)) 1) False))

(= (pm:step-on)
   (let $states (collapse (match &pm (pm:state $c $k $d $r) (pm:at $c $k)))
     (if (== $states ()) pm:halted
     (if (> (size-atom $states) 1) pm:more-than-one-state
       (let (pm:at $control $continuation) (car-atom $states)
         (if (and (== (get-metatype $control) Expression) (== (car-atom $control) pm:effect))
             (let ($_e $request) $control
               (if (pm:answered-in-turn $request $continuation) (pm:in-turn $request)
               (let $next (pm:performed $request)
                 (let $fired (if (== (car-atom $next) pm:alternatives)
                                 (let ($_a $items) $next
                                   (pm:fired
                                     (take (pm:state (pm:effect $q) $k $d $r))
                                     (pm:state (pm:answers-of $q $items) $k $d $r)))
                                 (if (== (car-atom $next) pm:go-on)
                                     ; the control the request itself names, with the
                                     ; state's own variables
                                     (pm:fired
                                       (take (pm:state (pm:effect (pm:tx-undo $then)) $k $d $r))
                                       (pm:state $then $k $d $r))
                                     (pm:fired
                                       (take (pm:state (pm:effect $q) $k $d $r))
                                       (pm:state $next $k $d $r))))
                   (if (== $fired ()) pm:stuck pm:stepped)))))
             (if (== (pm:fire (pm:key $control $continuation)) 0) pm:stuck pm:stepped)))))))

(= (pm:drive)
   (if (pm:spent) pm:out-of-fuel
     (if (pm:turn-over) pm:turn-over
       (let $outcome (pm:step)
         (if (== $outcome pm:stepped)
             (let $_n (change-state! &pm-count (+ (get-state &pm-count) 1)) (pm:drive))
             $outcome)))))

; A turn's bound is met between rules of the machine's own driving, never inside
; a request the host is answering in turn: the host's enumeration could not be
; taken up again.
(= (pm:turn-over)
   (let $end (get-state &pm-turn-end)
     (if (== $end none) False (>= (get-state &pm-count) $end))))

; Clear private bookkeeping on hosts that remove one or all matching rows.
(= (pm:clear-matching $space $pattern)
   (if (remove-atom $space $pattern) (pm:clear-matching $space $pattern) ()))

(= (pm:clear)
   (let* (($_s (pm:clear-matching &pm (pm:state $c $k $d $r)))
          ($_a (pm:clear-matching &pm (pm:alt $d2 $c2 $k2)))
          ($_d (pm:clear-matching &pm (pm:done $r2)))
          ($_g (pm:clear-matching &pm (pm:given $r6)))
          ($_b (pm:clear-matching &pm (pm:bag $d3 $items)))
          ($_below (pm:clear-matching &pm (pm:below $id5 $link5 $frames5)))
          ($_u (pm:clear-matching &pm (pm:unseen $d4)))
          ($_p (pm:clear-matching &pm-copies (pm:pending $id $item)))
          ($_j (pm:clear-matching &pm (pm:open-journals $open))))
     cleared))

; The answers of an expression, in order.  An error that nothing caught ends
; them: the answers given before it, then (pm:uncaught ERROR).  A machine that
; stopped for another reason answers (pm:no-end OUTCOME).
(: pm:answers (-> Atom %Undefined%))
(= (pm:answers $expression)
   (let* (($_begun (pm:begin $expression)) ($_run (pm:advance none))) (pm:end)))

; A run can be driven in turns.  The machine's whole state is rows of the store,
; so a run goes on where a turn left it.
;   (pm:begin EXPRESSION)   the machine at the start of EXPRESSION
;   (pm:advance STEPS)      fire rules until the run ends or STEPS more have fired
;                           (none: no bound); the outcome, pm:turn-over when the
;                           run goes on.  A turn after the end changes nothing.
;   (pm:end)                the answers, as pm:answers gives them; of a run that
;                           has not ended, (pm:no-end pm:turn-over)
(: pm:begin (-> Atom %Undefined%))
(= (pm:begin $expression)
   (let* (($_clear (pm:clear))
          ($_zero (change-state! &pm-count 0))
          ($start (pm:list2 pm:eval (pm:term $expression)))
          ($state (cons-atom pm:state (pm:list4 $start pm:halt z pm:nil)))
          ($_made (add-atom &pm $state))
          ($_none (add-atom &pm (pm:open-journals pm:nil)))
          ($_given (add-atom &pm (pm:given pm:nil)))
          ($_going (change-state! &pm-outcome pm:turn-over)))
     begun))

(= (pm:advance $steps)
   (if (== (get-state &pm-outcome) pm:turn-over)
       (let* (($_bound (change-state! &pm-turn-end
                         (if (== $steps none) none (+ (get-state &pm-count) $steps))))
              ($outcome (pm:drive))
              ($_kept (change-state! &pm-outcome $outcome))
              ($_unbound (change-state! &pm-turn-end none)))
         $outcome)
       (get-state &pm-outcome)))

(= (pm:end)
   (let $results (collapse (match &pm (pm:done $r) $r))
     (if (== $results ()) (pm:ending (get-state &pm-outcome))
       (pm:reversed (car-atom $results) ()))))

(= (pm:ending $outcome)
   (let $held (collapse (match &pm (pm:state (pm:uncaught $error) $k $d $r) $error))
     (if (== $held ()) (pm:list2 pm:no-end $outcome)
       (let $before (car-atom (collapse (match &pm (pm:given $answers) $answers)))
         (pm:reversed (pm:list3 pm:items (pm:list2 pm:uncaught (car-atom $held)) $before) ())))))

(= (pm:steps) (get-state &pm-count))

; What the host's space holds now is the machine's and the libraries' loaded
; before it, not the program's.
!(pm:hide-loaded!)
