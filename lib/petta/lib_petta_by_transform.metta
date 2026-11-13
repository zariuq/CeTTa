; PeTTa's own forms, each written with transform.
;
; Import transform first, then this library:
;
;   !(import! &self (library lib_transform))
;   !(import! &self (library lib_petta_by_transform))
;
; Each definition says what a PeTTa form is as a firing.  (pt:FORM ...) answers as
; (FORM ...) does in the selected variant and leaves the same store;
; tests/petta/petta_by_transform.metta
; states each as a law and checks it.
;
;   (pt:match S P T)          one firing at the call that reads every match of P;
;                             for a conjunction, its joint matches in that one
;                             state (the SWI variant of pt:read instead starts
;                             each leg when reached)
;   (pt:get-atoms S)          one firing that reads every atom
;   (pt:add-atom S A)         an empty row that makes A in S
;   (pt:remove-atom S A)      remove occurrences according to pt:variant
;   (pt:superpose (a b c))    an empty row with one instance per element
;   (pt:empty)                an empty row with no instance
;   (pt:let P E B)            every answer of E met with P, then B
;   (pt:let* ((P E) ...) B)   the same for each pair, in order
;   (pt:if C T E)             C's answer, and the branch whose guard holds
;   (pt:when C T)             the same without an else branch
;   (pt:fresh)                a fresh name: a stored counter taken and made again
;   (pt:case E ((P B) ...))   E's answer stored once; a row fires when its pattern
;                             meets it and no earlier row's does
;   (pt:collapse E)           every answer of E made under a fresh name, then one
;                             firing that takes them all
;   (pt:once E)               the first answer of E
;   (pt:define (= H B))       an equation as a stored rule
;   (pt:call (F A ...))       a read of F's rules whose head meets the call, each
;                             body run by the caller
;
; A firing hands the caller a row of bindings, and the caller binds them:
;
;   (pt:read S P)             a read of P that binds P's variables in the caller,
;                             once for each match: one firing at the call reads
;                             every match, and a conjunction is read part by
;                             part, each part when it is reached, as PeTTa
;                             does; (match S P T) is
;                             (let True (pt:read S (quote P)) T)
;   (pt:rule! F PATTERNS KEY)  a stored rule head: the argument patterns of one
;                             equation of F, as far as they are plain data, and
;                             the key of the equation
;   (pt:apply F VALUES)       a read of F's rule heads that meet the values; the
;                             caller runs (pt:body KEY (pt:args VALUE ...)) for each
;
; The row is (pt:row V ...), the variables of the pattern, so the instance is
; data whatever the values are: a value that is itself an expression is not
; evaluated again, and a value in a position the template holds is not quoted.
; The forms above that take code as an argument (pt:let, pt:if, pt:case,
; pt:collapse, pt:once, pt:call) run it with eval after its variables are
; bound; a value that is an evaluable expression is then evaluated again, so
; those forms are the form's meaning for values that are data.
;
; Arguments are held as written, as the forms hold theirs; the space argument of
; match, get-atoms, add-atom and remove-atom is evaluated, as theirs is.
;
; Sequencing, argument order and running a body are the caller's: a firing
; hands its instances to the code around it.

; The host variants differ in occurrence removal and conjunction snapshots.
; CeTTa removes one occurrence and reads a conjunction at one snapshot;
; SWI-PeTTa removes all matches and starts each conjunction leg when reached.
; Select the variant before running the program. A constant equation is
; shared with worker threads just like the rest of the imported definitions.
(= (pt:variant) cetta)
(= (pt:variant! cetta) (pt:set-variant cetta))
(= (pt:variant! swi) (pt:set-variant swi))
(= (pt:set-variant $variant)
   (let $_old (remove-atom &self (= (pt:variant) $previous))
     (add-atom &self (= (pt:variant) $variant))))

;; ---- building source lists and calls as data ----------------------------------------

(= (pt:list2 $a $b) (cons-atom $a (cons-atom $b ())))
(= (pt:list3 $a $b $c) (cons-atom $a (cons-atom $b (cons-atom $c ()))))
(= (pt:list4 $a $b $c $d) (cons-atom $a (cons-atom $b (cons-atom $c (cons-atom $d ())))))

;; ---- rows -------------------------------------------------------------------------

; The variables of a term, each once, in order of first appearance.
(= (pt:seen $variable $variables)
   (if (== $variables ()) False
     (let ($one $more) (decons-atom $variables)
       (if (== $one $variable) True (pt:seen $variable $more)))))

; The kind of a term.  A value the host holds that is no atom of the data
; model, such as the partial application (+ 1) once evaluated, has no metatype;
; it is a leaf.
(= (pt:kind $term)
   (case (get-metatype $term) (($kind $kind) (Empty pt:opaque))))

(= (pt:variables-onto $term $found)
   (case (pt:kind $term)
     ((Variable (if (pt:seen $term $found) $found (cons-atom $term $found)))
      (Expression (pt:variables-each $term $found))
      ($other $found))))

(= (pt:variables-each $terms $found)
   (if (== $terms ()) $found
     (let ($term $more) (decons-atom $terms)
       (pt:variables-each $more (pt:variables-onto $term $found)))))

(= (pt:reversed $items $onto)
   (if (== $items ()) $onto
     (let ($item $more) (decons-atom $items) (pt:reversed $more (cons-atom $item $onto)))))

(= (pt:row-of $term) (cons-atom pt:row (pt:reversed (pt:variables-onto $term ()) ())))

;; ---- reads ------------------------------------------------------------------------

; A pattern headed by a source word (read, take, absent, ...) is data to match,
; so it is read explicitly; the parts of a conjunction likewise.
(= (pt:source-words) (read take absent unique collect all once let tf:as tf:let-first))

(= (pt:source $p)
   (if (== (pt:kind $p) Expression)
       (if (== $p ()) $p
         (let $head (car-atom $p)
           (if (== (pt:kind $head) Variable) $p
             (if (== $head ,) (cons-atom , (pt:sources (cdr-atom $p)))
               (if (is-member $head (pt:source-words))
                   (pt:list2 read $p)
                   $p)))))
       $p))

(= (pt:sources $ps)
   (if (== $ps ()) ()
     (let ($p $more) (decons-atom $ps)
       (let $one (pt:source $p)
         (let $tail (pt:sources $more) (cons-atom $one $tail))))))

; PeTTa's match answers from the store as it is when the match is called: what
; the code that consumes its answers writes in between does not change them.
; So a read is one firing at the call, of every match (all); one branch for
; each match would be validated again when its turn comes, and a row the
; consumer removed meanwhile would be missed.
(= (pt:conjunction? $p)
   (if (== (pt:kind $p) Expression)
       (if (== $p ()) False
         (let $head (car-atom $p)
           (if (== (pt:kind $head) Variable) False (== $head ,))))
       False))

(= (pt:every $source)
   (if (pt:conjunction? $source)
       (let $parts (cdr-atom $source)
         (if (== $parts ()) $source
           (let ($first $more) (decons-atom $parts)
             (cons-atom , (cons-atom (pt:list2 all $first) $more)))))
       (pt:list2 all $source)))

(: pt:match (-> %Undefined% Atom Atom %Undefined%))
(= (pt:match $space $pattern $template)
   (let $source (pt:every (pt:source $pattern))
     (transform $space $source $template)))

(= (pt:get-atoms $space) (transform $space (all (read $atom)) $atom))

; A read that binds the pattern's variables in the caller: the firing answers
; the row of their values, and the row is met with the pattern's own.  PeTTa
; reads a conjunction at one snapshot in the CeTTa variant. In the SWI
; variant each leg starts as its preceding leg reaches it.
(= (pt:read $space $pattern)
   (if (and (== (pt:variant) swi) (pt:conjunction? $pattern))
       (pt:read-parts $space (cdr-atom $pattern))
       (let $row (pt:row-of $pattern)
         (let $source (pt:every (pt:source $pattern))
           (let $row (transform $space $source $row) True)))))

(= (pt:read-parts $space $parts)
   (if (== $parts ()) True
     (let ($part $more) (decons-atom $parts)
       (let True (pt:read $space $part) (pt:read-parts $space $more)))))

;; ---- writes -----------------------------------------------------------------------

; The atom is made as data: bound by a let, a template headed by ',' is one
; instance.  add-atom answers true.
; A space handle is the firing's destination.  Any other name is a space as
; soon as an atom is added under it, and a transform reads a destination that
; is no handle as a function; for such a name the firing answers the atom it
; makes to the caller, who stores it under the name.
(: pt:add-atom (-> %Undefined% Atom %Undefined%))
(= (pt:add-atom $space $atom)
   (if (is-space $space)
       (let $made (transform $space (let $new (quote $atom)) $new $space) True)
       (let (pt:made $value) (transform $space (let $new (quote $atom)) (pt:made $new))
         (add-atom $space $value))))

; The host variant determines occurrence multiplicity. Collecting the result
; keeps the removal pattern's bindings private in either variant.
(: pt:remove-atom (-> %Undefined% Atom %Undefined%))
(= (pt:remove-atom $space $atom)
   (let $source (pt:source $atom)
     (let $taken (if (== (pt:variant) swi)
                     (transform $space (collect (take $source) $all) $all)
                     (collapse (once (transform $space (take $source) True))))
       (not (== $taken ())))))

;; ---- choice -----------------------------------------------------------------------

; A written list: one instance for each element, evaluated by the caller.
(: pt:superpose (-> Atom %Undefined%))
(= (pt:superpose $elements)
   (let $template (cons-atom , $elements)
     (transform &self () $template)))

(= (pt:empty) (transform &self () (,)))

; The first answer.
(: pt:once (-> Atom %Undefined%))
(= (pt:once $expression)
   (transform &self (once (let $first $expression)) $first))

; A fresh name: the counter is a stored atom, taken and made again with the
; next number.  The names separate the atoms each evaluation keeps in the work
; space, where a program has no fresh space to keep them in.
!(bind! &pt-work (new-space))
!(add-atom &pt-work (pt:next 0))

(= (pt:renew $next $issued)
   (let $made (transform &pt-work (let $new (quote (pt:next $next))) $new &pt-work) $issued))

(= (pt:fresh)
   (if (pt:parallel?) (with_mutex pt:names (pt:fresh-now)) (pt:fresh-now)))

(= (pt:fresh-now)
   (transform &pt-work (, (take (pt:next $n)) (let $m (+ $n 1))) (pt:renew $m $n)))

; A host whose hyperpose runs its branches in parallel: the commit of a firing
; is a critical section there (tf:atomic!, of the transform written in PeTTa),
; and so is taking a fresh name: between the take of the counter and its
; renewal another branch would find no counter and go on with no name.
(= (pt:parallel!)
   (let $_commits (tf:atomic!) (add-atom &pt-work (pt:parallel))))
(= (pt:parallel?) (not (== (collapse (match &pt-work (pt:parallel) on)) ())))

; Every answer, made under a fresh name and taken together.
(= (pt:answer-values $atoms)
   (if (== $atoms ()) ()
     (let ($atom $more) (decons-atom $atoms)
       (let ($_answer $_name $value) $atom
         (let $tail (pt:answer-values $more) (cons-atom $value $tail))))))

(: pt:collapse (-> Atom %Undefined%))
(= (pt:collapse $expression)
   (let $name (pt:fresh)
     (let $made (transform &self (all (let $answer $expression)) (pt:answer $name $answer) &pt-work)
       (pt:answer-values
         (transform &pt-work (collect (take (pt:answer $name $any)) $all) $all)))))

;; ---- binding ----------------------------------------------------------------------

; Every answer of the expression is one row.
(: pt:let (-> Atom Atom Atom %Undefined%))
(= (pt:let $pattern $expression $body)
   (transform &self (all (let $pattern $expression)) $body))

(= (pt:let-sources $pairs)
   (if (== $pairs ()) ()
     (let ($pair $more) (decons-atom $pairs)
       (let ($pattern $expression) $pair
         (let $binding (pt:list2 all (pt:list3 let $pattern $expression))
           (let $tail (pt:let-sources $more) (cons-atom $binding $tail)))))))

(: pt:let* (-> Atom Atom %Undefined%))
(= (pt:let* $pairs $body)
   (let $sources (cons-atom , (pt:let-sources $pairs))
     (transform &self $sources $body)))

;; ---- branching --------------------------------------------------------------------

; The condition is evaluated once for each of its answers; of the two guarded
; firings one holds.
(: pt:if (-> Atom Atom Atom %Undefined%))
(= (pt:if $condition $then $else)
   (transform &self (all (let $c $condition))
     (, (transform &self (let True (== $c True)) $then)
        (transform &self (let False (== $c True)) $else))))

(: pt:when (-> Atom Atom %Undefined%))
(= (pt:when $condition $then)
   (transform &self (all (let $c $condition))
     (transform &self (let True (== $c True)) $then)))

; The value is stored once under a fresh name.  Row i fires when its pattern
; meets the value and no earlier row's pattern does: first match as a match and
; an absence.  The row that fires takes the value; a last firing takes it when
; no row did.  The rows' firings are built as data and then offered together.
(= (pt:case-firings $name $rows $earlier)
   (if (== $rows ())
       (cons-atom (pt:list2 pt:case-unmatched $name) ())
     (let ($row $more) (decons-atom $rows)
       (let ($pattern $body) $row
         (let $here (pt:list3 pt:value $name $pattern)
           (let $sources (cons-atom , (cons-atom (pt:list2 take $here) $earlier))
             (let $firing (pt:list4 transform &pt-work $sources $body)
               (let $guards (cons-atom (pt:list2 absent $here) $earlier)
                 (let $tail (pt:case-firings $name $more $guards)
                   (cons-atom $firing $tail))))))))))

(= (pt:case-unmatched $name)
   (transform &pt-work (take (pt:value $name $any)) (,)))

(: pt:case-value (-> %Undefined% Atom %Undefined%))
(= (pt:case-value $value $rows)
   (let $name (pt:fresh)
     (let $made (transform &pt-work (let $v (quote $value)) (pt:value $name $v) &pt-work)
       (let $firings (pt:case-firings $name $rows ())
         (let $template (cons-atom , $firings)
           (transform &self () $template))))))

; A row (Empty BODY) is taken when the expression has no answer at all.  With
; such a row the answers are collected first, by one firing, and the rows are
; then offered each answer in turn; without one, each answer is offered as it
; is found.
(: pt:case (-> Atom Atom %Undefined%))
(= (pt:case $expression $rows)
   (let (pt:rows $plain $empty) (pt:case-rows $rows)
     (if (== $empty pt:none)
         (transform &self (all (let $value $expression)) (pt:case-value $value $plain))
         (let (pt:some $body) $empty
           (let $answers (pt:collapse $expression)
             (pt:case-answers $answers $plain $body))))))

; (pt:rows PLAIN EMPTY): the rows without the Empty row, and (pt:some BODY) for
; the first Empty row, or pt:none.
(: pt:case-rows (-> Atom %Undefined%))
(= (pt:case-rows $rows)
   (if (== $rows ()) (pt:rows () pt:none)
     (let ($row $more) (decons-atom $rows)
       (let (pt:rows $plain $empty) (pt:case-rows $more)
         (let ($pattern $body) $row
           (if (== $pattern Empty)
               (pt:rows $plain (pt:list2 pt:some $body))
               (pt:rows (cons-atom $row $plain) $empty)))))))

(: pt:case-answers (-> %Undefined% Atom Atom %Undefined%))
(= (pt:case-answers $answers $rows $body)
   (if (== $answers ())
       (transform &self () $body)
       (let $value (superpose $answers) (pt:case-value $value $rows))))

;; ---- equations --------------------------------------------------------------------

; An equation is a stored rule keyed by its head symbol; a call reads the rules
; whose head meets it and runs each body.  The rule is read with a data template
; and its body is run by the caller.
!(bind! &pt-rules (new-space))

(: pt:define (-> Atom %Undefined%))
(= (pt:define $equation)
   (let ($_eq $head $body) $equation
     (let $name (car-atom $head)
       (let $arguments (cdr-atom $head)
         (let $rule (pt:list4 pt:rule $name $arguments $body)
           (let $made (transform &pt-rules (let $new (quote $rule)) $new &pt-rules) True))))))

(= (pt:run (pt:code $code)) (eval $code))

; The call's arguments are values: the caller evaluated them.  The rules that
; meet the call are read together, and then each body runs.
(= (pt:call-values $name $values)
   (pt:run (superpose (collapse (transform &pt-rules (pt:rule $name $values $body)
                                                   (pt:code $body))))))

; The arguments of a written call, each evaluated in order: every combination of
; their answers.
(= (pt:arguments $expressions)
   (if (== $expressions ()) ()
     (let ($expression $more) (decons-atom $expressions)
       (let $value (eval $expression)
         (let $tail (pt:arguments $more) (cons-atom $value $tail))))))

(: pt:call (-> Atom %Undefined%))
(= (pt:call $application)
   (let $name (car-atom $application)
     (let $values (pt:arguments (cdr-atom $application))
       (pt:call-values $name $values))))

;; ---- equations that stay compiled --------------------------------------------------

; A rule head: the name of an equation, its argument patterns as far as they are
; plain data, and the key of the equation.  The equation itself is an equation
; of pt:body for that key, with the argument patterns as written, so its head
; meets a call exactly as the equation's own head does.
;
; The patterns are stored as (pt:args P ...), never as the bare list (P ...): a
; list of argument values whose first value can be applied, such as
; ((+ 1) (1 2 3)), is itself an application wherever it is evaluated.
;
; A pattern is stored as far as it is plain data.  A part headed by a name the
; host runs or reads in a head (a function, cons) is met by the compiled
; equation's own head; the stored rule head holds a variable there.  This is
; decided when the rule is stored, as the host decides it when it compiles the
; equation.
(: pt:rule! (-> Atom Atom Atom %Undefined%))
(= (pt:rule! $name $patterns $key) (pt:store-rule $name $patterns $key))

; A function with a cut.  Each call has a mark of its own.  A body that cuts
; stores the mark, and the rule heads after it fire only while the mark is
; absent: the cut is a fact the call tells, and the later alternatives are
; guarded by its absence.  The host's own cut still discards the choices made
; earlier in the body.
(= (pt:apply-cut $name $values)
   (let* (($arguments (cons-atom pt:args $values))
          ($mark (pt:fresh)))
     (let (pt:fired $key)
          (transform &pt-rules
                     (, (pt:rule $name $arguments $k) (absent &pt-work (pt:cut $mark)))
                     (pt:fired $k))
       (pt:body-of $key $arguments $mark))))

; The equation of a key: one that cuts takes the call's mark as well.
(= (pt:body-of $key $arguments $mark)
   (let (pt:key $name $kind $index) $key
     (if (is-member $kind (static-cut dynamic-cut))
         (pt:body-cut $key $arguments $mark)
         (pt:body $key $arguments))))

(= (pt:cut! $mark) (pt:add-atom &pt-work (pt:cut $mark)))

; A call with further arguments, for a name called with more arguments than
; its rules take: the rule heads that meet the first values, one branch each,
; and the equation of that key called with the further values after its own.
; A host that widens an equation whose body is a partial application when it
; is defined (SWI-PeTTa) has the widened equation at that arity, and an
; equation it did not widen answers nothing there; a host that applies an
; answer to further arguments itself (CeTTa) does that.  An equation that cuts
; is no partial application: its answer is applied to the further values.
(= (pt:apply-further $name $values $further)
   (let* (($arguments (cons-atom pt:args $values))
          ($mark (pt:fresh)))
     (let (pt:fired $key)
          (transform &pt-rules
                     (, (pt:rule $name $arguments $k) (absent &pt-work (pt:cut $mark)))
                     (pt:fired $k))
       (pt:body-further $key $arguments $mark $further))))

(= (pt:body-further $key $arguments $mark $further)
   (let (pt:key $name $kind $index) $key
     (if (is-member $kind (static-cut dynamic-cut))
         (let* (($answer (pt:body-cut $key $arguments $mark))
                ($unapplied (cons-atom $answer $further))
                ($applied (reduce $unapplied)))
           (if (== $applied $unapplied) (empty) $applied))
         (let $call (cons-atom pt:body (cons-atom $key (cons-atom $arguments $further)))
           (reduce $call)))))

; One answer, pt:absent, when the mark is not stored; none when it is.
(= (pt:unmarked $mark) (transform &pt-work (absent (pt:cut $mark)) pt:absent))

; The same for a caller that holds the name, the patterns and the key as values.
(= (pt:store-rule $name $patterns $key)
   (let $plain (pt:plain-each $patterns)
     (let $rule (pt:list4 pt:rule $name (cons-atom pt:args $plain) $key)
       (let $made (transform &pt-rules (let $new (quote $rule)) $new &pt-rules) True))))

(= (pt:unknown) $fresh)

(= (pt:plain $pattern)
   (if (== (pt:kind $pattern) Expression)
       (if (== $pattern ()) $pattern
         (let $head (car-atom $pattern)
           (if (pt:plain-head $head) (pt:plain-each $pattern) (pt:unknown))))
       $pattern))

(= (pt:plain-each $patterns)
   (if (== $patterns ()) ()
     (let ($pattern $more) (decons-atom $patterns)
       (let $one (pt:plain $pattern)
         (let $tail (pt:plain-each $more) (cons-atom $one $tail))))))

; A name with equations and a name the host defines are both Grounded with no
; type of their own; a number or a string is Grounded with its type.
(= (pt:plain-head $head)
   (if (== (pt:kind $head) Grounded) (not (== (get-type $head) %Undefined%)) True))

; The equations of keys are added by programs.  One is here from the start, for
; a key no rule has, so that pt:body is a function of two arguments whatever a
; program stores.  SWI-PeTTa widens an equation whose body is a partial
; application; with only widened equations of pt:body it would take the call
; in pt:apply for a partial application too, and widen every function that
; reaches its equation through it.
(= (pt:body (pt:no-key) $arguments) (empty))

; The rule heads that meet the values, one branch each; the caller then runs the
; equation of that key on the values.
(= (pt:apply $name $values)
   (let $arguments (cons-atom pt:args $values)
     (let (pt:fired $key)
          (transform &pt-rules (pt:rule $name $arguments $k) (pt:fired $k))
       (pt:body $key $arguments))))
