; One step of the transform calculus, written in ordinary PeTTa.
;
; A state is a value.  PeTTa's variables are the frame's unknowns, PeTTa's
; unifier is the solver, and what a firing learns is the bindings it leaves:
;
;   (step:state NEXT NAMES APART PRESENT)
;     PRESENT   the present occurrences, (step:occ ID LABEL), oldest first
;     APART     the told disequations not yet decided, (step:apart L R)
;     NEXT      the next occurrence identity;  NAMES  how many names are minted
;
; A firing says what it touches and what it tells:
;
;   (step:firing TAKES READS MAKES TELLS APART GUARDS FRESH)
;     TAKES, READS   (step:use PATTERN ID): the pattern is the label of that occurrence
;     MAKES          labels of the occurrences made
;     TELLS          (step:equal L R): equations told
;     APART          (step:apart L R): disequations told
;     GUARDS         (step:equal A B)   A and B are equal at every solution
;                    (step:absent P)    no present label equals P at any solution
;                    (step:count P N)   exactly N present labels equal P, at every solution
;                    (step:region P N)  exactly N present labels are instances of P, the
;                                       same N at every solution (a predicate query on the pattern)
;                    (step:when E)      the expression E answers True; E is code held as
;                                       data, so a firing that holds one is written
;                                       quoted: (step:fire STATE (quote (step:firing ...)))
;     FRESH          the variables the firing uses for the names it mints: the step
;                    binds each to a new name
;
; The firing's own variables are its fresh variables.  A name is (step:name N),
; made by the step alone: a firing reaches a new name through a variable of
; FRESH and an old one through a label that holds it.  A number or a symbol is
; a structural code, the same wherever it is written.  A made label
; (step:schema LABEL) is a stored schema: its private variables are written
; (step:local 0), (step:local 1), ... and every take or read of it opens them
; afresh.
;
;   (step:fire STATE FIRING)   (step:admitted STATE2 FIRING2) with the firing as
;                              solved, or (step:refused REASON): shapes, fits,
;                              unsolvable, or (step:guard G)
;   (step:admits STATE FIRING) the same verdict, the state unchanged
;   (step:run STATE FIRINGS)   fires in order and stops at the first refusal:
;                              (step:ran COUNT STATE2)
;
; Admission is the calculus's: the takes are distinct and present, a taken
; occurrence is not also read, the equations of the takes, the reads and the
; tells have a solution, no told disequation is violated, and every guard holds
; at every solution (a guard that some solution fails refuses the firing).
; Nothing is scheduled here: which firing to try is the caller's.
;
; Rules choose their occurrences:
;
;   (step:rule TAKES READS MAKES TELLS APART GUARDS FRESH)   TAKES and READS are patterns
;   (step:candidates STATE RULE)   one firing for each choice of occurrences
;   (step:any STATE RULE)          one answer for each candidate that is admitted
;   (step:saturate STATE RULES)    fires the first admitted candidate until none is:
;                                  (step:halted COUNT STATE2)
;
; A rule with no unknown in the store and no name is one transform over a space:
;
;   (step:sources RULE)            its sources, (step:template RULE) its template
;   (step:fire-in SPACE RULE)      (transform SPACE sources template SPACE)

;; ---- lists --------------------------------------------------------------------------

(= (step:list2 $a $b) (cons-atom $a (cons-atom $b ())))
(= (step:list3 $a $b $c) (cons-atom $a (cons-atom $b (cons-atom $c ()))))

(= (step:append $xs $ys)
   (if (== $xs ()) $ys
     (let ($x $more) (decons-atom $xs)
       (let $tail (step:append $more $ys) (cons-atom $x $tail)))))

(= (step:length $xs) (if (== $xs ()) 0 (+ 1 (step:length (cdr-atom $xs)))))

;; ---- states -------------------------------------------------------------------------

(= (step:empty) (step:state 1 0 () ()))

; The label of occurrence ID: (step:found LABEL) or step:missing.
(= (step:label $present $id)
   (if (== $present ()) step:missing
     (let ($occurrence $more) (decons-atom $present)
       (let (step:occ $here $label) $occurrence
         (if (== $here $id) (step:found $label) (step:label $more $id))))))

(= (step:labels $present)
   (if (== $present ()) ()
     (let ($occurrence $more) (decons-atom $present)
       (let (step:occ $_id $label) $occurrence
         (let $tail (step:labels $more) (cons-atom $label $tail))))))

;; ---- names and schemas --------------------------------------------------------------

; Each variable of FRESH bound to a new name: the count of names after them.
(= (step:name-all $next $variables)
   (if (== $variables ()) $next
     (let ($variable $more) (decons-atom $variables)
       (let $variable (step:name $next) (step:name-all (+ $next 1) $more)))))

; A stored schema opened: each private variable becomes one new variable.
(= (step:variable) $new)

(= (step:local-value $index $opened)
   (if (== $opened ()) step:none
     (let ($pair $more) (decons-atom $opened)
       (let ($at $value) $pair
         (if (== $at $index) (step:some $value) (step:local-value $index $more))))))

; The kind of a term.  A value the host holds that is no atom of the data
; model, such as the partial application (+ 1) once evaluated, has no metatype;
; it is a leaf.
(= (step:kind $term)
   (case (get-metatype $term) (($kind $kind) (Empty step:opaque))))

; (step:opened TERM2 OPENED2): TERM with its locals replaced, OPENED the pairs so far.
(= (step:open-term $term $opened)
   (if (== (step:kind $term) Expression)
       (if (== $term ()) (step:opened $term $opened)
         (let ($head $rest) (decons-atom $term)
           (if (== (step:kind $head) Variable)
               (step:open-each $term $opened)
               (if (and (== $head step:local) (== (size-atom $term) 2))
                   (let $index (car-atom $rest)
                     (case (step:local-value $index $opened)
                       (((step:some $value) (step:opened $value $opened))
                        ($none (let $value (step:variable)
                                 (step:opened $value (cons-atom (step:list2 $index $value) $opened)))))))
                   (step:open-each $term $opened)))))
       (step:opened $term $opened)))

(= (step:open-each $terms $opened)
   (if (== $terms ()) (step:opened () $opened)
     (let ($term $more) (decons-atom $terms)
       (let (step:opened $one $opened2) (step:open-term $term $opened)
         (let (step:opened $tail $opened3) (step:open-each $more $opened2)
           (step:opened (cons-atom $one $tail) $opened3))))))

; A label as a port reads it: a schema opened at a scope of its own.
(= (step:read-label $label)
   (if (== (step:kind $label) Expression)
       (if (== $label ()) $label
         (let $head (car-atom $label)
           (if (== (step:kind $head) Variable) $label
             (if (and (== $head step:schema) (== (size-atom $label) 2))
                 (let (step:opened $opened $_pairs) (step:open-term (car-atom (cdr-atom $label)) ())
                   $opened)
                 $label))))
       $label))

;; ---- shapes and fit -----------------------------------------------------------------

(= (step:use-ids $uses)
   (if (== $uses ()) ()
     (let ($use $more) (decons-atom $uses)
       (let (step:use $_pattern $id) $use
         (let $tail (step:use-ids $more) (cons-atom $id $tail))))))

(= (step:distinct $ids)
   (if (== $ids ()) True
     (let ($id $more) (decons-atom $ids)
       (if (is-member $id $more) False (step:distinct $more)))))

(= (step:all-present $present $ids)
   (if (== $ids ()) True
     (let ($id $more) (decons-atom $ids)
       (if (== (step:label $present $id) step:missing) False (step:all-present $present $more)))))

(= (step:none-in $ids $others)
   (if (== $ids ()) True
     (let ($id $more) (decons-atom $ids)
       (if (is-member $id $others) False (step:none-in $more $others)))))

;; ---- information --------------------------------------------------------------------

; Each pattern unified with the label of its occurrence.  No answer when one
; does not unify.
(= (step:solve-uses $present $uses)
   (if (== $uses ()) step:solved
     (let ($use $more) (decons-atom $uses)
       (let (step:use $pattern $id) $use
         (let (step:found $label) (step:label $present $id)
           (let $pattern (step:read-label $label)
             (step:solve-uses $present $more)))))))

(= (step:solve-tells $tells)
   (if (== $tells ()) step:solved
     (let ($tell $more) (decons-atom $tells)
       (let (step:equal $left $right) $tell
         (let $left $right (step:solve-tells $more))))))

; The disequations still open, or step:violated when one has equal sides.
(= (step:still-apart $pairs)
   (if (== $pairs ()) ()
     (let ($pair $more) (decons-atom $pairs)
       (let (step:apart $left $right) $pair
         (if (== $left $right) step:violated
           (let $tail (step:still-apart $more)
             (if (== $tail step:violated) step:violated
               (if (=? $left $right) (cons-atom $pair $tail) $tail))))))))

;; ---- guards -------------------------------------------------------------------------

; How many labels are the pattern at every solution, and whether another could
; be at some solution: (step:tally SURE POSSIBLE).
(= (step:tally $pattern $labels $sure $possible)
   (if (== $labels ()) (step:tally $sure $possible)
     (let ($label $more) (decons-atom $labels)
       (let $read (step:read-label $label)
         (if (== $read $pattern)
             (step:tally $pattern $more (+ $sure 1) $possible)
             (if (=? $read $pattern)
                 (step:tally $pattern $more $sure (+ $possible 1))
                 (step:tally $pattern $more $sure $possible)))))))

; A label is an instance of the pattern at every solution when meeting the
; pattern leaves the label as it was; it may be one at some solution when the
; two unify at all: (step:tally SURE POSSIBLE) again.
(= (step:met $label $pattern) (let $pattern $label $label))

(= (step:region-tally $pattern $labels $sure $possible)
   (if (== $labels ()) (step:tally $sure $possible)
     (let ($label $more) (decons-atom $labels)
       (let $read (step:read-label $label)
         (let $met (collapse (step:met $read $pattern))
           (if (== $met ())
               (step:region-tally $pattern $more $sure $possible)
               (if (=alpha (car-atom $met) $read)
                   (step:region-tally $pattern $more (+ $sure 1) $possible)
                   (step:region-tally $pattern $more $sure (+ $possible 1)))))))))

(= (step:guard-holds $labels $guard)
   (case $guard
     (((step:equal $a $b) (== $a $b))
      ((step:absent $pattern)
        (let (step:tally $sure $possible) (step:tally $pattern $labels 0 0)
          (and (== $sure 0) (== $possible 0))))
      ((step:count $pattern $number)
        (let (step:tally $sure $possible) (step:tally $pattern $labels 0 0)
          (and (== $sure $number) (== $possible 0))))
      ((step:region $pattern $number)
        (let (step:tally $sure $possible) (step:region-tally $pattern $labels 0 0)
          (and (== $sure $number) (== $possible 0))))
      ((step:when $expression) (== (eval $expression) True))
      ($other False))))

; step:hold, or the first guard that does not.
(= (step:guards $labels $guards)
   (if (== $guards ()) step:hold
     (let ($guard $more) (decons-atom $guards)
       (if (step:guard-holds $labels $guard) (step:guards $labels $more) (step:guard $guard)))))

;; ---- the firing ---------------------------------------------------------------------

(= (step:remove-taken $present $taken)
   (if (== $present ()) ()
     (let ($occurrence $more) (decons-atom $present)
       (let (step:occ $id $_label) $occurrence
         (let $tail (step:remove-taken $more $taken)
           (if (is-member $id $taken) $tail (cons-atom $occurrence $tail)))))))

(= (step:made $next $labels)
   (if (== $labels ()) ()
     (let ($label $more) (decons-atom $labels)
       (let $tail (step:made (+ $next 1) $more)
         (cons-atom (step:occ $next $label) $tail)))))

; One trial under the firing's own solution.  It answers a verdict, or nothing
; when the equations have no solution.
(= (step:trial (step:state $next $names $apart $present)
               (step:firing $takes $reads $makes $tells $told $guards $fresh))
   (let* (($taken (step:use-ids $takes))
          ($read (step:use-ids $reads)))
     (if (not (step:distinct $taken)) (step:refused shapes)
       (if (not (and (step:all-present $present $taken)
                     (and (step:all-present $present $read) (step:none-in $taken $read))))
           (step:refused fits)
           (let* (($names2 (step:name-all $names $fresh))
                  ($_takes (step:solve-uses $present $takes))
                  ($_reads (step:solve-uses $present $reads))
                  ($_tells (step:solve-tells $tells))
                  ($open (step:still-apart (step:append $told $apart))))
             (if (== $open step:violated) (step:refused unsolvable)
               (let $verdict (step:guards (step:labels $present) $guards)
                 (if (== $verdict step:hold)
                     (let* (($kept (step:remove-taken $present $taken))
                            ($new (step:made $next $makes))
                            ($present2 (step:append $kept $new)))
                       (step:admitted
                         (step:state (+ $next (step:length $makes)) $names2 $open $present2)
                         (step:firing $takes $reads $makes $tells $told $guards $fresh)))
                     (step:refused $verdict)))))))))

(= (step:fire $state $firing)
   (let $verdicts (collapse (step:trial $state $firing))
     (if (== $verdicts ()) (step:refused unsolvable) (car-atom $verdicts))))

(= (step:admits $state $firing)
   (case (step:fire $state $firing)
     (((step:admitted $_state $_firing) step:admitted)
      ($refused $refused))))

(= (step:run-from $count $state $firings)
   (if (== $firings ()) (step:ran $count $state)
     (let ($firing $more) (decons-atom $firings)
       (case (step:fire $state $firing)
         (((step:admitted $state2 $_solved) (step:run-from (+ $count 1) $state2 $more))
          ($refused (step:ran $count $state)))))))

(= (step:run $state $firings) (step:run-from 0 $state $firings))

;; ---- rules choose their occurrences ---------------------------------------------------

; Every way to give each pattern an occurrence: takes distinct, and no read of a
; taken occurrence.
(= (step:choose $present $patterns $excluded $distinct)
   (if (== $patterns ()) ()
     (let ($pattern $more) (decons-atom $patterns)
       (let (step:occ $id $_label) (superpose $present)
         (if (is-member $id $excluded) (empty)
           (let $tail (step:choose $present $more (if $distinct (cons-atom $id $excluded) $excluded) $distinct)
             (cons-atom (step:use $pattern $id) $tail)))))))

(= (step:candidates (step:state $next $names $apart $present)
                    (step:rule $take-patterns $read-patterns $makes $tells $told $guards $fresh))
   (let $takes (step:choose $present $take-patterns () True)
     (let $reads (step:choose $present $read-patterns (step:use-ids $takes) False)
       (step:firing $takes $reads $makes $tells $told $guards $fresh))))

(= (step:any $state $rule)
   (let $firing (step:candidates $state $rule)
     (case (step:fire $state $firing)
       (((step:admitted $state2 $solved) (step:admitted $state2 $solved))
        ($refused (empty))))))

; The first admitted candidate of the first rule that has one.
(= (step:first-admitted $state $rules)
   (if (== $rules ()) step:none
     (let ($rule $more) (decons-atom $rules)
       (let $found (collapse (once (step:any $state $rule)))
         (if (== $found ()) (step:first-admitted $state $more) (car-atom $found))))))

(= (step:saturate-from $count $state $rules)
   (case (step:first-admitted $state $rules)
     (((step:admitted $state2 $_solved) (step:saturate-from (+ $count 1) $state2 $rules))
      ($none (step:halted $count $state)))))

(= (step:saturate $state $rules) (step:saturate-from 0 $state $rules))

;; ---- a rule as one transform ----------------------------------------------------------

(= (step:wrap-each $word $patterns)
   (if (== $patterns ()) ()
     (let ($pattern $more) (decons-atom $patterns)
       (let $tail (step:wrap-each $word $more)
         (cons-atom (step:list2 $word $pattern) $tail)))))

(= (step:tell-sources $tells)
   (if (== $tells ()) ()
     (let ($tell $more) (decons-atom $tells)
       (let (step:equal $left $right) $tell
         (let $tail (step:tell-sources $more)
           (cons-atom (step:list3 let $left (step:list2 quote $right)) $tail))))))

(= (step:guard-sources $guards $index)
   (if (== $guards ()) ()
     (let ($guard $more) (decons-atom $guards)
       (let $tail (step:guard-sources $more (+ $index 1))
         (case $guard
           (((step:equal $a $b)
              (cons-atom (step:list3 let True (step:list3 == (step:list2 quote $a) (step:list2 quote $b))) $tail))
            ((step:absent $pattern) (cons-atom (step:list2 absent $pattern) $tail))
            ((step:count $pattern $number)
              (let $collection (step:variable)
                (cons-atom (step:list3 collect $pattern $collection)
                  (cons-atom (step:list3 let $number (step:list2 size-atom $collection)) $tail))))
            ((step:region $pattern $number)
              (let $collection (step:variable)
                (cons-atom (step:list3 collect $pattern $collection)
                  (cons-atom (step:list3 let $number (step:list2 size-atom $collection)) $tail))))
            ((step:when $expression) (cons-atom (step:list3 let True $expression) $tail))
            ($other $tail)))))))

(= (step:sources (step:rule $takes $reads $makes $tells $told $guards $fresh))
   (cons-atom ,
     (step:append (step:wrap-each take $takes)
       (step:append (step:wrap-each read $reads)
         (step:append (step:tell-sources $tells) (step:guard-sources $guards 0))))))

(= (step:template (step:rule $takes $reads $makes $tells $told $guards $fresh))
   (cons-atom , $makes))

(= (step:fire-in $space $rule)
   (let $sources (step:sources $rule)
     (let $template (step:template $rule)
       (transform $space $sources $template $space))))
