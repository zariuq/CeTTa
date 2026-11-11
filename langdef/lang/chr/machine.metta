; Refined ground CHR as a machine over plain values.
;
; The machine follows the refined relation of Mettapedia/Languages/CHR/Operational:
; a stack of goal lists and occurrence scans, a store, a propagation history and
; the next occurrence identifier. One tick is one bounded step: finishing a goal
; list, running one goal, starting or ending a rule occurrence for the active
; constraint, or trying one partner candidate. Partner search is itself a task
; on the stack, so a run can stop after any tick and resume later.
;
; Data, as emitted by the preparer:
;   value       integer | "atom" | (ChrTuple values)
;   term        (ChrValue value) | (ChrVar index) | (ChrTupleTerm terms)
;   head        (ChrHead "name" arity terms)
;   expression  (ChrLiteral integer) | (ChrIndex index) | (ChrOperation operator left right)
;   operator    ChrAdd | ChrSubtract | ChrMultiply | ChrQuotient | ChrRemainder | ChrModulo
;   guard       (ChrIdentical term term) | (ChrDistinct term term) | (ChrCompare comparison expression expression)
;   comparison  ChrEqual | ChrDifferent | ChrLess | ChrLessEqual | ChrGreater | ChrGreaterEqual
;   goal        (ChrTell head) | (ChrAssign index expression) | ChrFailGoal
;   rule        (ChrRule kept removed guards body kind), kind ChrSimplifying | ChrPropagating
;   occurrence  (ChrAt rule-index head-index rule), heads numbered kept ++ removed
;   program     (ChrProgram ((ChrOccurrencesOf (ChrKey "name" arity) occurrences) ...))
;   seeds       a list of heads with value terms, posted in order
; Each key's occurrences are listed in the refined visit order: rules in order,
; removed heads before kept heads within a rule.
;
; Lists are ChrNil and (ChrCons first rest). An environment is a list of
; (ChrBind index value). Store slots are (ChrSlot key occurrences) with
; occurrences (ChrOcc id values), newest first; partners are tried in head order,
; newest first, never the active constraint or an earlier pick.

; ---------- lists ----------
(= (gslt:chr-machine:append $xs $ys)
   (case $xs ((ChrNil $ys) ((ChrCons $x $rest) (ChrCons $x (gslt:chr-machine:append $rest $ys))))))
(= (gslt:chr-machine:member $x $xs)
   (case $xs ((ChrNil False) ((ChrCons $y $rest) (if (== $x $y) True (gslt:chr-machine:member $x $rest))))))
(= (gslt:chr-machine:length $xs)
   (case $xs ((ChrNil 0) ((ChrCons $x $rest) (+ 1 (gslt:chr-machine:length $rest))))))
(= (gslt:chr-machine:drop $n $xs)
   (if (== $n 0) $xs
     (case $xs ((ChrNil ChrNil) ((ChrCons $x $rest) (gslt:chr-machine:drop (- $n 1) $rest))))))
(= (gslt:chr-machine:reverse $xs $done)
   (case $xs ((ChrNil $done) ((ChrCons $x $rest) (gslt:chr-machine:reverse $rest (ChrCons $x $done))))))

; ---------- environments, matching and instantiation ----------
(= (gslt:chr-machine:lookup $index $environment)
   (case $environment
     ((ChrNil ChrNone)
      ((ChrCons (ChrBind $other $value) $rest)
        (if (== $index $other) (ChrSome $value) (gslt:chr-machine:lookup $index $rest))))))

(= (gslt:chr-machine:match $term $value $environment)
   (case $term
     (((ChrValue $datum) (if (== $datum $value) (ChrSome $environment) ChrNone))
      ((ChrVar $index)
        (case (gslt:chr-machine:lookup $index $environment)
          ((ChrNone (ChrSome (ChrCons (ChrBind $index $value) $environment)))
           ((ChrSome $bound) (if (== $bound $value) (ChrSome $environment) ChrNone)))))
      ((ChrTupleTerm $terms)
        (case $value
          (((ChrTuple $fields) (gslt:chr-machine:match-all $terms $fields $environment))
           ($other ChrNone)))))))

(= (gslt:chr-machine:match-all $terms $values $environment)
   (case ($terms $values)
     (((ChrNil ChrNil) (ChrSome $environment))
      (((ChrCons $term $termRest) (ChrCons $value $valueRest))
        (case (gslt:chr-machine:match $term $value $environment)
          ((ChrNone ChrNone)
           ((ChrSome $bound) (gslt:chr-machine:match-all $termRest $valueRest $bound)))))
      ($other ChrNone))))

(= (gslt:chr-machine:instantiate $term $environment)
   (case $term
     (((ChrValue $datum) (ChrSome $datum))
      ((ChrVar $index) (gslt:chr-machine:lookup $index $environment))
      ((ChrTupleTerm $terms)
        (case (gslt:chr-machine:instantiate-all $terms $environment)
          ((ChrNone ChrNone) ((ChrSome $fields) (ChrSome (ChrTuple $fields)))))))))

(= (gslt:chr-machine:instantiate-all $terms $environment)
   (case $terms
     ((ChrNil (ChrSome ChrNil))
      ((ChrCons $term $rest)
        (case (gslt:chr-machine:instantiate $term $environment)
          ((ChrNone ChrNone)
           ((ChrSome $value)
             (case (gslt:chr-machine:instantiate-all $rest $environment)
               ((ChrNone ChrNone) ((ChrSome $values) (ChrSome (ChrCons $value $values))))))))))))

; ---------- integer arithmetic, as CHR.IntOperator.eval ----------
; Quotient and remainder truncate toward zero: they come from the long division
; of the magnitudes. Modulo is `%`, the floor remainder, with the divisor's sign.
(= (gslt:chr-machine:magnitude $n) (if (< $n 0) (- 0 $n) $n))

; For n >= 0 and d > 0: (ChrPair q r) with n = q * d + r and 0 <= r < d.
(= (gslt:chr-machine:divide $n $d)
   (if (< $n $d) (ChrPair 0 $n)
     (case (gslt:chr-machine:divide $n (+ $d $d))
       (((ChrPair $q $r)
          (if (< $r $d) (ChrPair (+ $q $q) $r) (ChrPair (+ (+ $q $q) 1) (- $r $d))))))))

; For b /= 0: (ChrPair quotient remainder), both truncating toward zero.
(= (gslt:chr-machine:truncate $a $b)
   (case (gslt:chr-machine:divide (gslt:chr-machine:magnitude $a) (gslt:chr-machine:magnitude $b))
     (((ChrPair $q $r)
        (ChrPair (if (== (< $a 0) (< $b 0)) $q (- 0 $q)) (if (< $a 0) (- 0 $r) $r))))))

(= (gslt:chr-machine:operate $operator $a $b)
   (case $operator
     ((ChrAdd (ChrOk (+ $a $b)))
      (ChrSubtract (ChrOk (- $a $b)))
      (ChrMultiply (ChrOk (* $a $b)))
      (ChrQuotient (if (== $b 0) (ChrFault ChrDivisionByZero)
        (case (gslt:chr-machine:truncate $a $b) (((ChrPair $q $r) (ChrOk $q))))))
      (ChrRemainder (if (== $b 0) (ChrFault ChrDivisionByZero)
        (case (gslt:chr-machine:truncate $a $b) (((ChrPair $q $r) (ChrOk $r))))))
      (ChrModulo (if (== $b 0) (ChrFault ChrDivisionByZero) (ChrOk (% $a $b)))))))

; A value is an integer, an atom (a string) or (ChrTuple values).
(= (gslt:chr-machine:integer $value)
   (case $value
     (((ChrTuple $fields) (ChrFault (ChrNonInteger $value)))
      ($other (if (== (get-type $other) Number) (ChrOk $other) (ChrFault (ChrNonInteger $other)))))))

(= (gslt:chr-machine:eval $expression $environment)
   (case $expression
     (((ChrLiteral $n) (ChrOk $n))
      ((ChrIndex $index)
        (case (gslt:chr-machine:lookup $index $environment)
          ((ChrNone (ChrFault (ChrUnbound $index)))
           ((ChrSome $value) (gslt:chr-machine:integer $value)))))
      ((ChrOperation $operator $left $right)
        (case (gslt:chr-machine:eval $left $environment)
          (((ChrFault $reason) (ChrFault $reason))
           ((ChrOk $a)
             (case (gslt:chr-machine:eval $right $environment)
               (((ChrFault $reason) (ChrFault $reason))
                ((ChrOk $b) (gslt:chr-machine:operate $operator $a $b)))))))))))

(= (gslt:chr-machine:holds $comparison $a $b)
   (case $comparison
     ((ChrEqual (== $a $b)) (ChrDifferent (not (== $a $b)))
      (ChrLess (< $a $b)) (ChrLessEqual (<= $a $b))
      (ChrGreater (> $a $b)) (ChrGreaterEqual (>= $a $b)))))

; ---------- guards, as CHR.Guard.eval and CHR.guardsEval ----------
(= (gslt:chr-machine:values $left $right $environment)
   (case (gslt:chr-machine:instantiate $left $environment)
     ((ChrNone (ChrFault (ChrUninstantiated $left)))
      ((ChrSome $a)
        (case (gslt:chr-machine:instantiate $right $environment)
          ((ChrNone (ChrFault (ChrUninstantiated $right)))
           ((ChrSome $b) (ChrOk (ChrPair $a $b)))))))))

(= (gslt:chr-machine:guard $guard $environment)
   (case $guard
     (((ChrIdentical $left $right)
        (case (gslt:chr-machine:values $left $right $environment)
          (((ChrFault $reason) (ChrFault $reason)) ((ChrOk (ChrPair $a $b)) (ChrOk (== $a $b))))))
      ((ChrDistinct $left $right)
        (case (gslt:chr-machine:values $left $right $environment)
          (((ChrFault $reason) (ChrFault $reason)) ((ChrOk (ChrPair $a $b)) (ChrOk (not (== $a $b)))))))
      ((ChrCompare $comparison $left $right)
        (case (gslt:chr-machine:eval $left $environment)
          (((ChrFault $reason) (ChrFault $reason))
           ((ChrOk $a)
             (case (gslt:chr-machine:eval $right $environment)
               (((ChrFault $reason) (ChrFault $reason))
                ((ChrOk $b) (ChrOk (gslt:chr-machine:holds $comparison $a $b))))))))))))

(= (gslt:chr-machine:guards $guards $environment)
   (case $guards
     ((ChrNil (ChrOk True))
      ((ChrCons $guard $rest)
        (case (gslt:chr-machine:guard $guard $environment)
          (((ChrFault $reason) (ChrFault $reason))
           ((ChrOk $holds) (if $holds (gslt:chr-machine:guards $rest $environment) (ChrOk False)))))))))

; ---------- the store ----------
(= (gslt:chr-machine:slot $key $store)
   (case $store
     ((ChrNil ChrNil)
      ((ChrCons (ChrSlot $other $occurrences) $rest)
        (if (== $other $key) $occurrences (gslt:chr-machine:slot $key $rest))))))
(= (gslt:chr-machine:put $key $occurrences $store)
   (case $store
     ((ChrNil (ChrCons (ChrSlot $key $occurrences) ChrNil))
      ((ChrCons (ChrSlot $other $old) $rest)
        (if (== $other $key) (ChrCons (ChrSlot $key $occurrences) $rest)
          (ChrCons (ChrSlot $other $old) (gslt:chr-machine:put $key $occurrences $rest)))))))
(= (gslt:chr-machine:without $id $occurrences)
   (case $occurrences
     ((ChrNil ChrNil)
      ((ChrCons (ChrOcc $other $values) $rest)
        (if (== $other $id) $rest (ChrCons (ChrOcc $other $values) (gslt:chr-machine:without $id $rest)))))))
(= (gslt:chr-machine:alive $id $occurrences)
   (case $occurrences
     ((ChrNil False)
      ((ChrCons (ChrOcc $other $values) $rest) (if (== $other $id) True (gslt:chr-machine:alive $id $rest))))))
(= (gslt:chr-machine:remove-picks $picks $store)
   (case $picks
     ((ChrNil $store)
      ((ChrCons (ChrPick $key $id) $rest)
        (gslt:chr-machine:remove-picks $rest
          (gslt:chr-machine:put $key (gslt:chr-machine:without $id (gslt:chr-machine:slot $key $store)) $store))))))
(= (gslt:chr-machine:ids $picks)
   (case $picks ((ChrNil ChrNil) ((ChrCons (ChrPick $key $id) $rest) (ChrCons $id (gslt:chr-machine:ids $rest))))))

(= (gslt:chr-machine:occurrences-of $key $table)
   (case $table
     ((ChrNil ChrNil)
      ((ChrCons (ChrOccurrencesOf $other $occurrences) $rest)
        (if (== $other $key) $occurrences (gslt:chr-machine:occurrences-of $key $rest))))))

; ---------- the machine ----------
; (ChrState tasks store history next status seeds); status ChrRunning,
; ChrFailed or (ChrFaulted reason); seeds are the seed identifiers, newest first.

(= (gslt:chr-machine:start $seeds)
   (ChrState (ChrCons (ChrSeeds $seeds) ChrNil) ChrNil ChrNil 0 ChrRunning ChrNil))

(= (gslt:chr-machine:with-status $state $status)
   (case $state
     (((ChrState $tasks $store $history $next $old $seeds) (ChrState $tasks $store $history $next $status $seeds)))))

; Introduce a constraint: give it the next identifier, store it and activate it
; before the goals that follow.
(= (gslt:chr-machine:introduce $program $head $environment $continue $original $tail $store $history $next $status $seeds)
   (case $head
     (((ChrHead $name $arity $arguments)
        (case (gslt:chr-machine:instantiate-all $arguments $environment)
          ((ChrNone (ChrState $original $store $history $next
                      (ChrFaulted (ChrUninstantiated (ChrTupleTerm $arguments))) $seeds))
           ((ChrSome $values)
             (case $program
               (((ChrProgram $table)
                  (let $key (ChrKey $name $arity)
                    (ChrState
                      (ChrCons (ChrScan $next $key $values (gslt:chr-machine:occurrences-of $key $table))
                        (ChrCons $continue $tail))
                      (gslt:chr-machine:put $key (ChrCons (ChrOcc $next $values) (gslt:chr-machine:slot $key $store)) $store)
                      $history (+ $next 1) $status $seeds))))))))))))

(= (gslt:chr-machine:goal $program $goal $environment $goals $tail $store $history $next $status $seeds)
   (case $goal
     (((ChrTell $head)
        (gslt:chr-machine:introduce $program $head $environment (ChrGoals $environment $goals)
          (ChrCons (ChrGoals $environment (ChrCons $goal $goals)) $tail) $tail $store $history $next $status $seeds))
      ((ChrAssign $index $expression)
        (case (gslt:chr-machine:eval $expression $environment)
          (((ChrFault $reason)
             (ChrState (ChrCons (ChrGoals $environment (ChrCons $goal $goals)) $tail) $store $history $next
               (ChrFaulted $reason) $seeds))
           ((ChrOk $value)
             (case (gslt:chr-machine:lookup $index $environment)
               ((ChrNone (ChrState (ChrCons (ChrGoals (ChrCons (ChrBind $index $value) $environment) $goals) $tail)
                           $store $history $next $status $seeds))
                ((ChrSome $old)
                  (if (== $old $value)
                    (ChrState (ChrCons (ChrGoals $environment $goals) $tail) $store $history $next $status $seeds)
                    (ChrState (ChrCons (ChrGoals $environment (ChrCons $goal $goals)) $tail) $store $history $next
                      ChrFailed $seeds)))))))))
      (ChrFailGoal
        (ChrState (ChrCons (ChrGoals $environment (ChrCons $goal $goals)) $tail) $store $history $next
          ChrFailed $seeds)))))

; The candidates for one head position: only the active constraint fills its own
; position; any other position draws from the store slot of its key.
(= (gslt:chr-machine:candidates $index $position $active $values $head $store)
   (if (== $index $position) (ChrCons (ChrOcc $active $values) ChrNil)
     (case $head (((ChrHead $name $arity $arguments) (gslt:chr-machine:slot (ChrKey $name $arity) $store))))))

(= (gslt:chr-machine:scan $active $key $values $remaining $tail $store $history $next $status $seeds)
   (if (not (gslt:chr-machine:alive $active (gslt:chr-machine:slot $key $store)))
     (ChrState $tail $store $history $next $status $seeds)
     (case $remaining
       ((ChrNil (ChrState $tail $store $history $next $status $seeds))
        ((ChrCons (ChrAt $ruleIndex $position (ChrRule $kept $removed $guards $body $kind)) $after)
          (let* (($heads (gslt:chr-machine:append $kept $removed))
                 ($first (case $heads (((ChrCons $head $rest) $head)))))
            (ChrState
              (ChrCons (ChrSearch $active $key $values (ChrAt $ruleIndex $position (ChrRule $kept $removed $guards $body $kind)) $after
                         (ChrCons (ChrFrame 0 $heads
                                    (gslt:chr-machine:candidates 0 $position $active $values $first $store)
                                    ChrNil ChrNil ChrNil) ChrNil))
                $tail)
              $store $history $next $status $seeds)))))))

; A complete selection fires when it is fresh for a propagating rule and its
; guards hold in order; a faulting guard stops the run.
(= (gslt:chr-machine:fire $active $key $values $occurrence $after $environment $picks $tail $store $history $next $status $seeds)
   (case $occurrence
     (((ChrAt $ruleIndex $position (ChrRule $kept $removed $guards $body $kind))
        (let* (($removedPicks (gslt:chr-machine:drop (gslt:chr-machine:length $kept) $picks))
               ($activeRemoved (gslt:chr-machine:member $active (gslt:chr-machine:ids $removedPicks)))
               ($continuation (if $activeRemoved $tail
                                (ChrCons (ChrScan $active $key $values (ChrCons $occurrence $after)) $tail)))
               ($updated (case $kind ((ChrPropagating (ChrCons (ChrFired $ruleIndex (gslt:chr-machine:ids $picks)) $history))
                                       (ChrSimplifying $history)))))
          (ChrState (ChrCons (ChrGoals $environment $body) $continuation)
            (gslt:chr-machine:remove-picks $removedPicks $store) $updated $next $status $seeds))))))

(= (gslt:chr-machine:search $active $key $values $occurrence $after $frames $tail $store $history $next $status $seeds)
   (case $frames
     ((ChrNil (ChrState (ChrCons (ChrScan $active $key $values $after) $tail) $store $history $next $status $seeds))
      ((ChrCons (ChrFrame $index $heads $candidates $environment $picks $used) $below)
        (case $candidates
          ((ChrNil
             (ChrState (ChrCons (ChrSearch $active $key $values $occurrence $after $below) $tail)
               $store $history $next $status $seeds))
           ((ChrCons (ChrOcc $id $arguments) $others)
             (let $stay (ChrCons (ChrFrame $index $heads $others $environment $picks $used) $below)
               (case $occurrence
                 (((ChrAt $ruleIndex $position (ChrRule $kept $removed $guards $body $kind))
                    (if (or (gslt:chr-machine:member $id $used) (and (not (== $index $position)) (== $id $active)))
                      (ChrState (ChrCons (ChrSearch $active $key $values $occurrence $after $stay) $tail)
                        $store $history $next $status $seeds)
                      (case $heads
                        (((ChrCons (ChrHead $name $arity $terms) $later)
                           (case (gslt:chr-machine:match-all $terms $arguments $environment)
                             ((ChrNone
                                (ChrState (ChrCons (ChrSearch $active $key $values $occurrence $after $stay) $tail)
                                  $store $history $next $status $seeds))
                              ((ChrSome $bound)
                                (let $chosen (gslt:chr-machine:append $picks (ChrCons (ChrPick (ChrKey $name $arity) $id) ChrNil))
                                  (case $later
                                    ((ChrNil
                                       (gslt:chr-machine:complete $active $key $values $occurrence $after $stay
                                         $bound $chosen $guards $kind $ruleIndex $tail $store $history $next $status $seeds))
                                     ((ChrCons $nextHead $rest)
                                       (ChrState
                                         (ChrCons (ChrSearch $active $key $values $occurrence $after
                                                    (ChrCons (ChrFrame (+ $index 1) $later
                                                               (gslt:chr-machine:candidates (+ $index 1) $position $active $values $nextHead $store)
                                                               $bound $chosen (ChrCons $id $used))
                                                      $stay))
                                           $tail)
                                         $store $history $next $status $seeds)))))))))))))))))))))))

(= (gslt:chr-machine:complete $active $key $values $occurrence $after $stay $environment $picks $guards $kind $ruleIndex $tail $store $history $next $status $seeds)
   (let $retry (ChrState (ChrCons (ChrSearch $active $key $values $occurrence $after $stay) $tail)
                 $store $history $next $status $seeds)
     (if (case $kind ((ChrPropagating (gslt:chr-machine:member (ChrFired $ruleIndex (gslt:chr-machine:ids $picks)) $history))
                      (ChrSimplifying False)))
       $retry
       (case (gslt:chr-machine:guards $guards $environment)
         (((ChrFault $reason) (gslt:chr-machine:with-status $retry (ChrFaulted $reason)))
          ((ChrOk $holds)
            (if $holds
              (gslt:chr-machine:fire $active $key $values $occurrence $after $environment $picks $tail $store $history $next $status $seeds)
              $retry)))))))

(= (gslt:chr-machine:tick $program $state)
   (case $state
     (((ChrState $tasks $store $history $next ChrRunning $seeds)
        (case $tasks
          ((ChrNil $state)
           ((ChrCons $task $tail)
             (case $task
               (((ChrSeeds ChrNil) (ChrState $tail $store $history $next ChrRunning $seeds))
                ((ChrSeeds (ChrCons $head $heads))
                  (gslt:chr-machine:introduce $program $head ChrNil (ChrSeeds $heads) $tasks $tail $store $history $next
                    ChrRunning (ChrCons $next $seeds)))
                ((ChrGoals $environment ChrNil) (ChrState $tail $store $history $next ChrRunning $seeds))
                ((ChrGoals $environment (ChrCons $goal $goals))
                  (gslt:chr-machine:goal $program $goal $environment $goals $tail $store $history $next ChrRunning $seeds))
                ((ChrScan $active $key $values $remaining)
                  (gslt:chr-machine:scan $active $key $values $remaining $tail $store $history $next ChrRunning $seeds))
                ((ChrSearch $active $key $values $occurrence $after $frames)
                  (gslt:chr-machine:search $active $key $values $occurrence $after $frames $tail $store $history $next
                    ChrRunning $seeds))))))))
      ($stopped $stopped))))

; At most `budget` ticks; a finished or stopped state is returned unchanged.
(= (gslt:chr-machine:advance $program $state $budget)
   (if (== $budget 0) $state
     (case $state
       (((ChrState (ChrCons $task $tail) $store $history $next ChrRunning $seeds)
          (gslt:chr-machine:advance $program (gslt:chr-machine:tick $program $state) (- $budget 1)))
        ($finished $finished)))))

; ---------- the package port ----------
(= (gslt:chr-machine:begin $program $seeds) (ChrRun $program (gslt:chr-machine:start $seeds)))
(= (gslt:chr-machine:unit $run $budget)
   (case $run (((ChrRun $program $state) (ChrRun $program (gslt:chr-machine:advance $program $state $budget))))))
(= (gslt:chr-machine:outcome $run)
   (case $run
     (((ChrRun $program (ChrState $tasks $store $history $next $status $seeds))
        (let $posted (gslt:chr-machine:reverse $seeds ChrNil)
          (case $status
            ((ChrRunning
               (case $tasks
                 ((ChrNil (LangPortDoneV1 (ChrMachineResult ChrComplete $store $posted)))
                  ($pending LangPortPendingV1))))
             (ChrFailed (LangPortDoneV1 (ChrMachineResult ChrFailed $store $posted)))
             ((ChrFaulted $reason) (LangPortDoneV1 (ChrMachineResult (ChrFaulted $reason) $store $posted))))))))))
