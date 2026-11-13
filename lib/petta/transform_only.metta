; A PeTTa program rewritten so that its space operations and its equation calls
; are firings of transform.
;
;   (to:statement FORM)   one top-level form: prints the forms that replace it
;   (to:query FORM)       one !-query: prints the query that replaces it
;   (to:statement-text TEXT), (to:query-text TEXT)   the same for a form as text,
;                         which keeps the form as written: a form given as code
;                         has its lambdas named before it is seen
;   (to:expression E)     an expression as data, rewritten
;
; The rewritten program imports lib_transform, lib_petta_by_transform and this
; library (for code that is built and run at run time).
;
;   (match S P T)         (let True (to:read S (quote P)) T): a read whose row of
;                         bindings the caller binds, then T as written
;   (add-atom S A)        (to:add-atom S A): an empty row that makes A
;   (remove-atom S A)     (to:remove-atom S A): one firing that takes every match
;   (get-atoms S)         (to:get-atoms S)
;                         (with &self written for S: to:read-own, to:add-own,
;                         to:remove-own, to:own-atoms, the program's own store)
;   (= (F P ...) BODY)    a stored rule head (pt:rule! F (P ...) KEY), the
;                         equation itself as (= (pt:body KEY (pt:args P ...)) BODY),
;                         and one equation of F for its arity that reads the
;                         rule heads
;
; A firing hands the caller a row of bindings as data and the caller binds them,
; so a value is never evaluated again and a held position holds the value.
;
; The program's own space is a store of its atoms, &pt-own: its facts, its
; equations and its declarations, in order.  &self written in the program
; names that store (but for import!, which loads into the host's &self); an
; equation added to it or removed from it is also defined or undefined.  An
; equation or a declaration is kept there as (pt:own ATOM), because a host may
; take a bare equation row in any space for a definition; match and get-atoms
; on the store see the atom as written.  The host's &self holds the libraries
; and the compiled equations, which are no atoms of the program.  A library
; imported as written sees the host's &self, and a function as written that
; is handed the store sees the kept rows.
;
; What stays with the evaluator: the order in which a call's arguments are
; evaluated, binding (let, let*), branching (if, case), choice (superpose,
; collapse, once), the grounded operations, and running a body.
; (to:leave space) and (to:leave calls) leave a group as written.
;
; (to:also parallel) is for a host whose hyperpose runs its branches in
; parallel (SWI-PeTTa).  There the commit of a firing, the taking of a fresh
; name and a definition made while the program runs (several firings) are
; critical sections of the host (with_mutex).
;
; (to:also binding), (to:also branching) and (to:also choice) rewrite those
; forms too.  Each part of such a form that is code becomes an equation of a
; generated function (to:k SITE V ... ARGUMENT), V ... the variables of the
; part, and the form becomes a call of it; a call is a firing, so:
;
;   (let P E B)           (to:k SITE V ... E) with (= (to:k SITE V ... P) B): the
;                         read of the one rule head, met with E's value
;   (let* ((P E) ...) B)  the lets, nested
;   (if C T E)            (to:k SITE V ... (== C True)) with one equation for
;                         True and one for False
;   (case E ((P B) ...))  (to:k SITE V ... E) with one equation for each row;
;                         every row but the last cuts, so the first row whose
;                         pattern meets the value is the one taken.  A row
;                         (Empty B) is a second alternative, taken when no
;                         value of E stored the form's mark
;   (superpose (a b ...)) (to:k SITE V ...) with one equation for each element
;   (collapse E)          (pt:collapse THUNK), the thunk a call
;                         (to:thunk SITE (quote (pt:args V ...))) with
;                         (= (to:thunk SITE (pt:args V ...)) E)
;
; once stays with the evaluator in every mode: it asks for one answer and no
; more, and one firing over E would run the effects of every alternative.
;
; No value is held as code that way: a value reaches a generated function as an
; argument.  A part that cuts is left as written, since its cut belongs to the
; function it is written in.  unique, union, intersection and subtraction take
; a (superpose LIST) as part of their form; it stays.
; A part is then a function of its own, entered when it is reached: where a
; body defines a function and later mentions it as data, the part entered
; after the definition calls it.
;
; A pattern, a quoted term, an atom given to add-atom or remove-atom and an
; argument a type declaration holds as Atom are data and are left as written.
; A function declared to answer an Atom keeps its equations: its body is its
; answer, unevaluated.

!(bind! &to (new-space))
!(add-atom &to (to:rules 0))

; A rewritten program's own space: its atoms in order.  It is bound here, in
; the library whose functions compare a space with it.
!(bind! &pt-own (new-space))

(: to:leave (-> Atom %Undefined%))
(= (to:leave $group) (add-atom &to (to:left $group)))
(: to:also (-> Atom %Undefined%))
(= (to:also $group)
   (let $_host (if (== $group parallel) (pt:parallel!) as-it-is)
     (add-atom &to (to:added $group))))

(= (to:rewrites $group)
   (if (is-member $group (quote (space calls)))
       (== (collapse (match &to (to:left $group) $group)) ())
       (not (== (collapse (match &to (to:added $group) $group)) ()))))

;; ---- building expressions as data ---------------------------------------------------

(= (to:list1 $a) (cons-atom $a ()))
(= (to:list2 $a $b) (cons-atom $a (cons-atom $b ())))
(= (to:list3 $a $b $c) (cons-atom $a (cons-atom $b (cons-atom $c ()))))
(= (to:list4 $a $b $c $d) (cons-atom $a (cons-atom $b (cons-atom $c (cons-atom $d ())))))

; An expression headed by a name.  A name the language defines has the metatype
; Grounded, as a number has; a number in head position rewrites nothing.
(= (to:symbol-headed $e)
   (if (== (get-metatype $e) Expression)
       (if (== $e ()) False (is-member (get-metatype (car-atom $e)) (Symbol Grounded)))
       False))

(= (to:second $e) (car-atom (cdr-atom $e)))

;; ---- expressions --------------------------------------------------------------------

(= (to:expression $e)
   (if (to:symbol-headed $e)
       (to:form (car-atom $e) (size-atom $e) $e)
       (if (== (get-metatype $e) Expression) (to:each $e) (to:leaf $e))))

; &self written in the program is the program's own store.
(= (to:leaf $e)
   (if (== $e &self) (if (to:rewrites space) &pt-own $e) $e))

(= (to:each $items)
   (if (== $items ()) ()
     (let ($item $more) (decons-atom $items)
       (let $one (to:expression $item)
         (let $tail (to:each $more) (cons-atom $one $tail))))))

; HEAD with its arguments rewritten, but for the positions a declaration holds
; as Atom.
(= (to:application $head $e)
   (let $_widened (to:widen $head (- (size-atom $e) 1)) (to:call $head $e)))

(= (to:call $head $e)
   (let $held (collapse (match &to (to:holds $head $index) $index))
     (if (== $held ()) (to:each $e)
       (cons-atom $head (to:arguments (cdr-atom $e) 1 $held)))))

(= (to:arguments $items $index $held)
   (if (== $items ()) ()
     (let ($item $more) (decons-atom $items)
       (let $one (if (is-member $index $held) $item (to:expression $item))
         (let $tail (to:arguments $more (+ $index 1) $held) (cons-atom $one $tail))))))

;; ---- a call with more arguments than its function is defined with ---------------------

; SWI-PeTTa widens a function whose body is a partial application when the
; function is defined: (= (add-to $x) (+ $x)) takes two arguments.  A rewritten
; function's body is the stored equation of a key, which that host widens in
; the same way; the function's name needs an equation at the wider arity,
; made here at the first call with more arguments than every arity the
; function is defined with.  It reads the rule heads at each smaller arity
; and calls the equation of the key with the further arguments after its own
; (pt:apply-further).  A declaration with as many inputs as the call has
; arguments describes the wider call, and one with fewer does not, as in the
; program as written.
;
; What stays different on SWI-PeTTa: called with exactly the arguments it is
; written with, such a function answers a partial application of itself as
; written; rewritten it answers nothing there, because the widened equation
; of its key is no longer at the arity its rule is read at.  A body is not
; wrapped to prevent that: a wrapper delays the unification of the answer,
; and a function run backwards, as in (let (plus $a $b) four ...), would then
; search without its answer.
(= (to:exceeded $head $count)
   (let $arities (collapse (match &to (to:defined $head $arity) $arity))
     (if (== $arities ()) False (< (to:largest $arities) $count))))

(= (to:defined-below $name $count)
   (not (== (collapse (let $arity (match &to (to:defined $name $defined) $defined)
                        (if (< $arity $count) below (empty))))
            ())))

(= (to:largest $numbers)
   (let ($first $more) (decons-atom $numbers)
     (if (== $more ()) $first
       (let $other (to:largest $more) (if (< $first $other) $other $first)))))

(= (to:widen $name $count)
   (if (and (to:rewrites calls) (not (to:answers-atom? $name)))
       (if (to:exceeded $name $count)
           (if (== (collapse (match &to (to:widened $name $count) known)) ())
               (let* (($_noted (add-atom &to (to:widened $name $count)))
                      ($dispatch (to:widened-dispatch $name $count)))
                 (if (to:printing?)
                     (let $_said (to:print-query (to:list3 add-atom &to (to:list3 to:widened $name $count)))
                       (to:print $dispatch))
                     (add-atom &self $dispatch)))
               known)
           fits)
       as-written))

(= (to:widened-dispatch $name $count)
   (let $variables (to:variables $count)
     (to:list3 = (cons-atom $name $variables)
               (to:list3 to:further $name (to:list-code $variables)))))

; The first COUNT items of a list, and the others.
(= (to:split-at $count $items)
   (if (== $count 0) (to:list2 () $items)
     (let ($item $more) (decons-atom $items)
       (let ($front $rest) (to:split-at (- $count 1) $more)
         (to:list2 (cons-atom $item $front) $rest)))))

(= (to:further $name $values)
   (let* (($count (size-atom $values))
          ($arity (match &to (to:defined $name $defined) $defined))
          (True (< $arity $count))
          (($front $rest) (to:split-at $arity $values)))
     (pt:apply-further $name $front $rest)))

(= (to:form $head $size $e)
   (case (to:key $head $size)
     (((to:key match 4) (to:match $e))
      ((to:key add-atom 3) (to:write to:add-own to:add-atom $e))
      ((to:key remove-atom 3) (to:write to:remove-own to:remove-atom $e))
      ((to:key get-atoms 2)
        (if (to:rewrites space)
            (if (== (to:second $e) &self) (to:list1 to:own-atoms)
              (to:list2 to:get-atoms (to:expression (to:second $e))))
            (to:list2 (car-atom $e) (to:expression (to:second $e)))))
      ((to:key superpose 2) (to:superpose $e))
      ((to:key unique 2) (to:stream $e))
      ((to:key union 3) (to:stream $e))
      ((to:key intersection 3) (to:stream $e))
      ((to:key subtraction 3) (to:stream $e))
      ((to:key empty 1) (if (to:rewrites choice) (to:list1 pt:empty) $e))
      ((to:key once 2) (to:list2 (car-atom $e) (to:expression (to:second $e))))
      ((to:key collapse 2) (to:held pt:collapse $e))
      ((to:key let 4) (to:let $e))
      ((to:key let* 3) (to:let* $e))
      ((to:key if 4) (to:if $e))
      ((to:key if 3) (to:when $e))
      ((to:key case 3) (to:case $e))
      ((to:key quote 2) $e)
      ((to:key import! 3) $e)
      ((to:key cut 1)
        (if (to:rewrites calls) (to:list3 progn (to:list2 pt:cut! to:mark) $e) $e))
      ((to:key eval 2) (to:list2 to:eval (to:second $e)))
      ($other (to:application $head $e)))))

; A read: the firing answers the row of the pattern's bindings and pt:read binds
; them in the caller; the template is then ordinary code.
(= (to:match $e)
   (let ($match $space $pattern $template) $e
     (if (to:rewrites space)
         (to:list4 let True
           (if (== $space &self)
               (to:list2 to:read-own (to:list2 quote $pattern))
               (to:list3 to:read (to:expression $space) (to:list2 quote $pattern)))
           (to:expression $template))
         (to:list4 $match (to:expression $space) $pattern (to:expression $template)))))

; A write to &self as written is a write to the program's own store, known
; here; a write to any other space expression is decided when it runs.
(= (to:write $own $any $e)
   (let ($written $space $atom) $e
     (if (to:rewrites space)
         (if (== $space &self)
             (to:list2 $own $atom)
             (to:list3 $any (to:expression $space) $atom))
         (to:list3 $written (to:expression $space) $atom))))

;; ---- generated functions ----------------------------------------------------------

; The variables of a term, each once, in order of first appearance.
(= (to:seen $variable $variables)
   (if (== $variables ()) False
     (let ($one $more) (decons-atom $variables)
       (if (== $one $variable) True (to:seen $variable $more)))))

(= (to:variables-onto $term $found)
   (case (get-metatype $term)
     ((Variable (if (to:seen $term $found) $found (cons-atom $term $found)))
      (Expression (to:variables-each $term $found))
      ($other $found))))

(= (to:variables-each $terms $found)
   (if (== $terms ()) $found
     (let ($term $more) (decons-atom $terms)
       (to:variables-each $more (to:variables-onto $term $found)))))

(= (to:reversed $items $onto)
   (if (== $items ()) $onto
     (let ($item $more) (decons-atom $items) (to:reversed $more (cons-atom $item $onto)))))

(= (to:variables-of $term) (to:reversed (to:variables-onto $term ()) ()))

(= (to:append $front $back)
   (if (== $front ()) $back
     (let ($item $more) (decons-atom $front)
       (let $tail (to:append $more $back) (cons-atom $item $tail)))))

; The forms of a program are printed as it is rewritten; code rewritten while a
; program runs is defined in the running program.
(= (to:printing?) (not (== (collapse (match &to (to:printing) known)) ())))
(= (to:printing!) (if (to:printing?) known (add-atom &to (to:printing))))

; A site: the number of one generated function.  Sites made while a program
; runs are kept apart from the ones printed with it.
(= (to:site)
   (let $index (to:next-index)
     (if (to:printing?) $index (to:list2 dynamic $index))))

; One equation of a generated function, defined before the form that calls it.
(= (to:lift! $equation)
   (if (to:printing?) (to:equation $equation) (to:define $equation)))

(= (to:k-head $site $variables $last)
   (cons-atom to:k (cons-atom $site (to:append $variables $last))))

; A group's forms become generated functions when the group is rewritten and
; the code does not cut.
(= (to:lifts $group $code) (if (to:rewrites $group) (not (to:cuts $code)) False))

; A written list is one alternative per element; any other argument computes a
; list value, whose elements are data.
(= (to:superpose $e)
   (let ($superpose $elements) $e
     (if (== (get-metatype $elements) Expression)
         (if (to:computes-a-list $elements)
             (to:superpose-written $e)
             (if (to:lifts choice $elements)
                 (if (== $elements ()) (to:list1 pt:empty)
                   (let* (($site (to:site))
                          ($variables (to:variables-of $elements))
                          ($_defined (to:superpose-equations $site $variables $elements)))
                     (to:k-head $site $variables ())))
                 (to:superpose-written $e)))
         $e)))

; superpose kept, its elements rewritten.
(= (to:superpose-written $e)
   (let ($superpose $elements) $e
     (if (== (get-metatype $elements) Expression)
         (to:list2 $superpose
           (if (to:computes-a-list $elements) (to:expression $elements) (to:each $elements)))
         $e)))

; unique, union, intersection and subtraction take their streams as written: a
; (superpose LIST) directly under one is part of the form and stays.
(= (to:written-superpose $e)
   (if (to:symbol-headed $e)
       (and (== (car-atom $e) superpose) (== (size-atom $e) 2))
       False))

(= (to:streams $items)
   (if (== $items ()) ()
     (let ($item $more) (decons-atom $items)
       (let $one (if (to:written-superpose $item) (to:superpose-written $item) (to:expression $item))
         (let $tail (to:streams $more) (cons-atom $one $tail))))))

(= (to:stream $e) (cons-atom (car-atom $e) (to:streams (cdr-atom $e))))

(= (to:superpose-equations $site $variables $elements)
   (if (== $elements ()) done
     (let ($element $more) (decons-atom $elements)
       (let $_one (to:lift! (to:list3 = (to:k-head $site $variables ()) $element))
         (to:superpose-equations $site $variables $more)))))

(= (to:computes-a-list $e)
   (if (to:symbol-headed $e)
       (let $head (car-atom $e)
         (if (is-member $head (quote (cons-atom cdr-atom sort-atom msort unique-atom union-atom
                                      intersection-atom subtraction-atom reverse append map-atom
                                      filter-atom get-atoms collapse)))
             True
             (not (== (collapse (match &to (to:defined $head $arity) $arity)) ()))))
       False))

; collapse holds its expression; held, it is a thunk whose arguments are
; quoted, so that no value in it is evaluated again.
(= (to:held $form $e)
   (let ($written $code) $e
     (if (to:lifts choice $code)
         (let* (($site (to:site))
                ($variables (to:variables-of $code))
                ($arguments (cons-atom pt:args $variables))
                ($_defined (to:lift! (to:list3 = (to:list3 to:thunk $site $arguments) $code))))
           (to:list2 $form (to:list3 to:thunk $site (to:list2 quote $arguments))))
         (to:list2 $written (to:expression $code)))))

(= (to:let $e)
   (let ($let $pattern $expression $body) $e
     (if (to:lifts binding $body)
         (let* (($site (to:site))
                ($variables (to:variables-of (to:list2 $pattern $body)))
                ($_defined (to:lift! (to:list3 = (to:k-head $site $variables (to:list1 $pattern))
                                               $body))))
           (to:k-head $site $variables (to:list1 (to:expression $expression))))
         (to:list4 $let $pattern (to:expression $expression) (to:expression $body)))))

(= (to:pairs $pairs)
   (if (== $pairs ()) ()
     (let ($pair $more) (decons-atom $pairs)
       (let $one (if (== (get-metatype $pair) Expression)
                     (if (== (size-atom $pair) 2)
                         (to:list2 (car-atom $pair) (to:expression (to:second $pair)))
                         $pair)
                     $pair)
         (let $tail (to:pairs $more) (cons-atom $one $tail))))))

(= (to:all-pairs $pairs)
   (if (== $pairs ()) True
     (let ($pair $more) (decons-atom $pairs)
       (if (== (get-metatype $pair) Expression)
           (if (== (size-atom $pair) 2) (to:all-pairs $more) False)
           False))))

(= (to:nested-lets $pairs $body)
   (if (== $pairs ()) $body
     (let ($pair $more) (decons-atom $pairs)
       (to:list4 let (car-atom $pair) (to:second $pair) (to:nested-lets $more $body)))))

(= (to:let* $e)
   (let ($let* $pairs $body) $e
     (if (== (get-metatype $pairs) Expression)
         (if (and (to:lifts binding (to:list2 $pairs $body)) (to:all-pairs $pairs))
             (to:expression (to:nested-lets $pairs $body))
             (to:list3 $let* (to:pairs $pairs) (to:expression $body)))
         $e)))

(= (to:if $e)
   (let ($if $condition $then $else) $e
     (if (to:lifts branching (to:list2 $then $else))
         (let* (($site (to:site))
                ($variables (to:variables-of (to:list2 $then $else)))
                ($_then (to:lift! (to:list3 = (to:k-head $site $variables (to:list1 True)) $then)))
                ($_else (to:lift! (to:list3 = (to:k-head $site $variables (to:list1 False)) $else))))
           (to:k-head $site $variables
             (to:list1 (to:list3 == (to:expression $condition) True))))
         (to:list4 $if (to:expression $condition) (to:expression $then) (to:expression $else)))))

(= (to:when $e)
   (let ($if $condition $then) $e
     (if (to:lifts branching $then)
         (let* (($site (to:site))
                ($variables (to:variables-of $then))
                ($_then (to:lift! (to:list3 = (to:k-head $site $variables (to:list1 True)) $then))))
           (to:k-head $site $variables
             (to:list1 (to:list3 == (to:expression $condition) True))))
         (to:list3 $if (to:expression $condition) (to:expression $then)))))

; (to:rows PLAIN EMPTY): the rows without the Empty row, and (to:some BODY) for
; the first Empty row, or to:none.
(= (to:case-rows $rows)
   (if (== $rows ()) (to:rows () to:none)
     (let ($row $more) (decons-atom $rows)
       (let (to:rows $plain $empty) (to:case-rows $more)
         (if (== (car-atom $row) Empty)
             (to:rows $plain (to:list2 to:some (to:second $row)))
             (to:rows (cons-atom $row $plain) $empty))))))

; Every row but the last cuts once its pattern has met the value.
(= (to:case-equations $site $variables $rows)
   (if (== $rows ()) done
     (let ($row $more) (decons-atom $rows)
       (let* (($body (to:second $row))
              ($code (if (== $more ()) $body (to:list3 progn (quote (cut)) $body)))
              ($_one (to:lift! (to:list3 = (to:k-head $site $variables (to:list1 (car-atom $row)))
                                           $code))))
         (to:case-equations $site $variables $more)))))

(= (to:case-written $case $expression $rows)
   (to:list3 $case (to:expression $expression) (to:pairs $rows)))

; With a row (Empty BODY) the key's values are still met one at a time: each
; value stores a mark and is then offered to the other rows; a second
; alternative, tried when the values are done, runs BODY if the mark is not
; stored.  The key is then code of a generated function, so a key that cuts is
; left as written.
(= (to:case-lifted $case $expression $rows)
   (let (to:rows $plain $empty) (to:case-rows $rows)
     (if (== $empty to:none)
         (if (== $plain ()) (to:case-written $case $expression $rows)
           (let* (($site (to:site))
                  ($variables (to:variables-of $plain))
                  ($_defined (to:case-equations $site $variables $plain)))
             (to:k-head $site $variables (to:list1 (to:expression $expression)))))
         (if (to:cuts $expression) (to:case-written $case $expression $rows)
           (let* (((to:some $body) $empty)
                  ($mark (to:variable))
                  ($value (to:variable))
                  ($noted (to:variable))
                  ($offered (if (== $plain ()) (to:list1 empty) (to:list3 $case $value $plain))))
             (to:expression
               (to:list4 let $mark (to:list1 pt:fresh)
                 (to:list2 superpose
                   (to:list2
                     (to:list4 let $value $expression
                       (to:list4 let $noted (to:list2 pt:cut! $mark) $offered))
                     (to:list4 let pt:absent (to:list2 pt:unmarked $mark) $body))))))))))

(= (to:case $e)
   (let ($case $expression $rows) $e
     (if (== (get-metatype $rows) Expression)
         (if (and (to:lifts branching $rows) (to:all-pairs $rows))
             (to:case-lifted $case $expression $rows)
             (to:case-written $case $expression $rows))
         $e)))

;; ---- statements -------------------------------------------------------------------

; Each printed line is one form of the rewritten program; (to:bang FORM) stands
; for the query !FORM.
(= (to:print $form) (println! $form))
(= (to:print-query $form) (println! (to:list2 to:bang $form)))

; A fresh variable for each call.
(= (to:variable) $fresh)

(= (to:variables $count)
   (if (<= $count 0) ()
     (let $one (to:variable)
       (let $tail (to:variables (- $count 1)) (cons-atom $one $tail)))))

; (cons-atom v1 (cons-atom v2 ())) as code: the list of the values, built
; without evaluating a value in head position.
(= (to:list-code $variables)
   (if (== $variables ()) ()
     (let ($v $more) (decons-atom $variables)
       (to:list3 cons-atom $v (to:list-code $more)))))

; ---- cut ---------------------------------------------------------------------
; A body that cuts.  (cut) is found by its shape; it is never evaluated here.
(= (to:cuts $e)
   (if (== (get-metatype $e) Expression)
       (if (== $e ()) False
         (if (and (== (size-atom $e) 1) (== (car-atom $e) cut)) True (to:cuts-any $e)))
       False))

(= (to:cuts-any $items)
   (if (== $items ()) False
     (let ($item $more) (decons-atom $items)
       (if (to:cuts $item) True (to:cuts-any $more)))))

; The compiled equation of a key.  One that cuts takes the call's mark as a
; third argument; its (cut) stores the mark and then cuts.
(= (to:compiled $key $patterns $body $cut)
   (if $cut
       (let $mark (to:variable)
         (to:list3 = (to:list4 pt:body-cut $key (cons-atom pt:args $patterns) $mark)
                     (to:replace (to:expression $body) to:mark $mark)))
       (to:list3 = (to:list3 pt:body $key (cons-atom pt:args $patterns)) (to:expression $body))))

(= (to:replace $e $old $new)
   (if (== (get-metatype $e) Expression)
       (to:replace-each $e $old $new)
       (if (== $e $old) $new $e)))

(= (to:replace-each $items $old $new)
   (if (== $items ()) ()
     (let ($item $more) (decons-atom $items)
       (let $one (to:replace $item $old $new)
         (let $tail (to:replace-each $more $old $new) (cons-atom $one $tail))))))

; The dispatch equation of a function with a cut gives each call its mark.
(= (to:dispatch-cut $name $arity)
   (let $variables (to:variables $arity)
     (to:list3 = (cons-atom $name $variables)
               (to:list3 pt:apply-cut $name (to:list-code $variables)))))

(= (to:cut-dispatch! $name $arity)
   (let $known (collapse (match &to (to:cuts-at $name $arity) known))
     (if (== $known ())
         (let* (($_noted (add-atom &to (to:cuts-at $name $arity)))
                ($plain (to:dispatch $name $arity))
                ($_gone (remove-atom &self $plain))
                ($cutting (to:dispatch-cut $name $arity))
                ($_made (add-atom &self $cutting)))
           True)
         known)))

; The one equation of a name and arity: it reads the rule heads.
(= (to:dispatch $name $arity)
   (let $variables (to:variables $arity)
     (to:list3 = (cons-atom $name $variables)
               (to:list3 pt:apply $name (to:list-code $variables)))))

(= (to:next-index)
   (let $count (match &to (to:rules $n) $n)
     (let $next (+ $count 1)
       (let $_removed (remove-atom &to (to:rules $count))
         (let $_added (add-atom &to (to:rules $next)) $count)))))

; A type declaration: the argument positions held as Atom, and a result held as
; Atom.
(= (to:note-types $name $types $index)
   (let ($type $more) (decons-atom $types)
     (if (== $more ())
         (if (== $type Atom)
             (let $_added (add-atom &to (to:answers-atom $name))
               (to:print-query (to:list3 add-atom &to (to:list2 to:answers-atom $name))))
             done)
         (let $_noted (if (== $type Atom)
                          (let $_added (add-atom &to (to:holds $name $index))
                            (to:print-query (to:list3 add-atom &to (to:list3 to:holds $name $index))))
                          skipped)
           (to:note-types $name $more (+ $index 1))))))

(= (to:declaration $form)
   (let ($_colon $name $type) $form
     (if (to:symbol-headed $type)
         (if (and (== (car-atom $type) ->) (> (size-atom $type) 1))
             (to:note-types $name (cdr-atom $type) 1)
             done)
         done)))

(= (to:answers-atom? $name)
   (not (== (collapse (match &to (to:answers-atom $name) $name)) ())))

; The dispatch equation of a name and arity, once.  For a function of the
; program it is printed with the program, as text: libraries of the host that
; work on a function's definition (memoising, tabling) expect it there.  For a
; generated function (to:k, to:thunk) the request is printed instead and the
; equation is added when the rewritten program runs, by this same check: code
; rewritten while the program runs makes generated functions too, and may
; have made the equation by then; two of them would answer every call twice.
; (add-atom holds its atom, so the equation is built first.)
(= (to:generated $name) (is-member $name (quote (to:k to:thunk))))

(= (to:ensure-dispatch $name $arity $static)
   (let $known (collapse (match &to (to:defined $name $arity) known))
     (if (== $known ())
         (let $_noted (add-atom &to (to:defined $name $arity))
           (let $dispatch (to:dispatch $name $arity)
             (if $static
                 (if (to:generated $name)
                     (to:print-query (to:list4 to:ensure-dispatch $name $arity False))
                     (let $_said (to:print-query (to:list3 add-atom &to (to:list3 to:defined $name $arity)))
                       (to:print $dispatch)))
                 (add-atom &self $dispatch))))
         known)))

(= (to:equation $form)
   (let ($_eq $head $body) $form
     (if (to:symbol-headed $head)
         (let $name (car-atom $head)
           (if (and (to:rewrites calls) (not (to:answers-atom? $name)))
               (let* (($patterns (cdr-atom $head))
                      ($cut (to:cuts $body))
                      ($key (to:list4 pt:key $name (if $cut static-cut static) (to:next-index)))
                      ($_dispatch (to:ensure-dispatch $name (size-atom $patterns) True))
                      ($_cut (if $cut
                                 (to:print-query (to:list3 to:cut-dispatch! $name (size-atom $patterns)))
                                 False))
                      ($compiled (to:compiled $key $patterns $body $cut))
                      ($_body (to:print $compiled)))
                 (to:print-query (to:list4 pt:rule! $name $patterns $key)))
               (let* (($arity (- (size-atom $head) 1))
                      ($_noted (add-atom &to (to:defined $name $arity))))
                 (to:print (to:list3 = $head (to:expression $body))))))
         (to:print (to:list3 = $head (to:expression $body))))))

(: to:statement (-> Atom %Undefined%))
(= (to:statement $form) (to:statement-form $form))
(= (to:statement-text $text) (to:statement-form (parse $text)))

(= (to:statement-form $form)
   (let $_printing (to:printing!) (to:statement-static $form)))

; A form of the program is an atom of its own store.  An equation is also
; compiled and a declaration is also given to the host; any other atom is kept
; in the own store only.
(= (to:own-row $form)
   (if (to:rewrites space) (to:print-query (to:list3 add-atom &pt-own (to:kept $form))) skipped))

(= (to:fact $form)
   (if (to:rewrites space) (to:own-row $form) (to:print $form)))

(= (to:statement-static $form)
   (if (to:symbol-headed $form)
       (case (to:key (car-atom $form) (size-atom $form))
         (((to:key = 3) (let $_own (to:own-row $form) (to:equation $form)))
          ((to:key : 3)
            (let* (($_noted (to:declaration $form)) ($_own (to:own-row $form))) (to:print $form)))
          ($other (to:fact $form))))
       (to:fact $form)))

(: to:query (-> Atom %Undefined%))
(= (to:query $form) (to:query-form $form))
(= (to:query-text $text) (to:query-form (parse $text)))
(= (to:query-form $form)
   (let $_printing (to:printing!) (to:print-query (to:expression $form))))

;; ---- the program's own store -------------------------------------------------------

(= (to:is-own $space) (if (== $space &pt-own) True (== $space &self)))

(= (to:headed-by $atom $heads)
   (if (to:symbol-headed $atom) (is-member (car-atom $atom) $heads) False))

; How an atom is kept: an equation or a declaration wrapped, any other as it is.
(= (to:kept $atom)
   (if (to:headed-by $atom (quote (= :))) (to:list2 pt:own $atom) $atom))

(= (to:wrapped $row)
   (if (== (get-metatype $row) Expression)
       (if (== (size-atom $row) 2) (== (car-atom $row) pt:own) False)
       False))

(= (to:unwrap $row) (if (to:wrapped $row) (to:second $row) $row))

; The atoms of the own store as written, in order.
(= (to:own-atoms) (let $row (pt:get-atoms &pt-own) (to:unwrap $row)))

(= (to:get-atoms $space)
   (if (to:is-own $space) (to:own-atoms) (pt:get-atoms $space)))

; The pattern that meets the kept form of what PATTERN meets; the parts of a
; conjunction each.
(= (to:own-pattern $pattern)
   (if (to:symbol-headed $pattern)
       (if (== (car-atom $pattern) ,)
           (cons-atom , (to:own-patterns (cdr-atom $pattern)))
           (to:kept $pattern))
       $pattern))

(= (to:own-patterns $patterns)
   (if (== $patterns ()) ()
     (let ($pattern $more) (decons-atom $patterns)
       (let $one (to:own-pattern $pattern)
         (let $tail (to:own-patterns $more) (cons-atom $one $tail))))))

; A read that binds the pattern's variables in the caller.  On the own store a
; pattern that is a variable meets every atom as written.
(= (to:read-own $pattern)
   (if (== (get-metatype $pattern) Variable)
       (let $pattern (to:own-atoms) True)
       (pt:read &pt-own (to:own-pattern $pattern))))

(= (to:read $space $pattern)
   (if (to:is-own $space) (to:read-own $pattern) (pt:read $space $pattern)))

;; ---- code built at run time ---------------------------------------------------------

; Code held as data is rewritten when it is run.
(: to:eval (-> Atom %Undefined%))
(= (to:eval $code)
   (let $rewritten (to:expression $code) (eval $rewritten)))

; An equation added to the program's own space is a definition.
(: to:add-atom (-> %Undefined% Atom %Undefined%))
(= (to:add-atom $space $atom)
   (if (to:is-own $space) (to:add-own $atom) (pt:add-atom $space $atom)))

; An atom added to the program's own store: an equation is also defined, a
; declaration is also given to the host.  An unbound atom is the host's to
; refuse.  Parallel branches (hyperpose) that define functions would interleave
; the firings that store their rule heads, so with (to:also parallel) a
; definition is a critical section.
(: to:add-own (-> Atom %Undefined%))
(= (to:add-own $atom)
   (if (== (get-metatype $atom) Variable) (add-atom &pt-own $atom) (to:add-own-bound $atom)))

(= (to:add-own-bound $atom)
   (let* (($kept (to:kept $atom))
          ($_host (if (to:is-equation $atom) (to:define-in-turn $atom)
                    (if (to:headed-by $atom (quote (:))) (add-atom &self $atom) kept))))
     (pt:add-atom &pt-own $kept)))

(= (to:define-in-turn $atom)
   (if (to:rewrites parallel)
       (with_mutex to:definitions (to:define $atom))
       (to:define $atom)))

(= (to:is-equation $atom)
   (if (to:symbol-headed $atom)
       (and (== (car-atom $atom) =) (== (size-atom $atom) 3))
       False))

(= (to:define $equation)
   (let ($_eq $head $body) $equation
     (if (to:symbol-headed $head)
         (let* (($name (car-atom $head))
                ($patterns (cdr-atom $head))
                ($cut (to:cuts $body))
                ($key (to:list4 pt:key $name (if $cut dynamic-cut dynamic) (to:next-index)))
                ($_dispatch (to:ensure-dispatch $name (size-atom $patterns) False))
                ($_cut (if $cut (to:cut-dispatch! $name (size-atom $patterns)) False))
                ($compiled (to:compiled $key $patterns $body $cut))
                ($_body (add-atom &self $compiled)))
           (pt:store-rule $name $patterns $key))
         (add-atom &self $equation))))

; An equation removed from the program's own space: the compiled equations it
; meets go, and their rule heads with them.
(: to:remove-atom (-> %Undefined% Atom %Undefined%))
(= (to:remove-atom $space $atom)
   (if (to:is-own $space) (to:remove-own $atom) (pt:remove-atom $space $atom)))

; An atom removed from the program's own store: an equation is also undefined.
; An unbound atom is the host's to refuse.
(: to:remove-own (-> Atom %Undefined%))
(= (to:remove-own $atom)
   (if (== (get-metatype $atom) Variable) (remove-atom &pt-own $atom) (to:remove-own-bound $atom)))

(= (to:remove-own-bound $atom)
   (let $kept (to:kept $atom)
     (if (to:is-equation $atom)
         (let $gone (to:undefine $atom)
           (let $_row (pt:remove-atom &pt-own $kept) $gone))
         (pt:remove-atom &pt-own $kept))))

(= (to:undefine $equation)
   (let ($_eq $head $body) $equation
     (if (to:symbol-headed $head)
         (let* (($name (car-atom $head))
                ($patterns (cdr-atom $head))
                ($key (to:list4 pt:key $name $kind $index))
                ($compiled (to:compiled $key $patterns $body (to:cuts $body)))
                ($keys (collapse (match &self $compiled $key)))
                ($gone (to:forget $name $keys))
                ($_retired (to:retire $name (size-atom $patterns))))
           $gone)
         (remove-atom &self $equation))))

; A name with no equation left at an arity is no function there: its dispatch
; equation goes, and a call of it is data again.
(= (to:retire $name $arity)
   (let $left (collapse (match &pt-rules (pt:rule $name $arguments $key) (size-atom $arguments)))
     (if (is-member (+ $arity 1) $left) False
       (let* (($dispatch (to:dispatch $name $arity))
              ($_dispatch (remove-atom &self $dispatch))
              ($cutting (to:dispatch-cut $name $arity))
              ($_cutting (remove-atom &self $cutting))
              ($noted (to:list3 to:defined $name $arity))
              ($_noted (remove-atom &to $noted))
              ($noted-cut (to:list3 to:cuts-at $name $arity))
              ($_noted-cut (remove-atom &to $noted-cut))
              ($_wider (to:narrow $name (collapse (match &to (to:widened $name $count) $count)))))
         True))))

; The dispatch equations of calls wider than every arity the name still has.
(= (to:narrow $name $counts)
   (if (== $counts ()) done
     (let ($count $more) (decons-atom $counts)
       (let $_one (if (to:defined-below $name $count) kept
                    (let* (($dispatch (to:widened-dispatch $name $count))
                           ($_dispatch (remove-atom &self $dispatch))
                           ($noted (to:list3 to:widened $name $count))
                           ($_noted (remove-atom &to $noted)))
                      gone))
         (to:narrow $name $more)))))

(= (to:forget $name $keys)
   (if (== $keys ()) False
     (let ($key $more) (decons-atom $keys)
       (let* (($_rule (pt:remove-atom &pt-rules (pt:rule $name $patterns $key)))
              ($_body (remove-atom &self (= (pt:body $key $arguments) $body)))
              ($_cutting (remove-atom &self (= (pt:body-cut $key $arguments2 $mark) $body2)))
              ($_rest (to:forget $name $more)))
         True))))
