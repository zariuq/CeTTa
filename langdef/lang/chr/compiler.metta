; Ground CHR (C0) compiler operations in MeTTa: read a selected catalogue,
; prepare a run from captured occurrences, and observe a finished machine run.
; Uses reader.metta, lowering.metta and the refined machine library.

; ---- Read -------------------------------------------------------------------

(= (c0:sentences (node $label $fields))
   (if (== $label chr:sentences-more)
     (case (c0:fields $fields)
       ((($sentence $rest) (cons-atom (c0:sentence-term $sentence) (c0:sentences $rest)))))
     ()))

(= (c0:reader-message $reason)
   (case $reason
     ((InvalidCharacterCode "a character code escape names no Unicode scalar value")
      ((UnknownEscape $escape) "the source holds an escape the reader does not define")
      ((UnknownNumber $number) "the source holds a number form the reader does not define")
      ($other "the source reader met an unexpected term"))))

(= (c0:read-reading $result)
   (case $result
     (((result (node chr:program (pair $sentences nil)) $more)
        (let $terms (c0:sentences $sentences)
          (case (c0:find-error-in $terms)
            (((Some $reason) (C0Error (c0:reader-message $reason)))
             ($none
               (let $forms (c0:forms $terms 0)
                 (if (c0:error? $forms) $forms (c0:assemble $forms))))))))
      ($other (C0Error "expected an explicitly selected whole-input parser result")))))

; Every selected whole-input interpretation must lower to the same program.
(= (c0:read-all $results $first)
   (case $results
     ((() $first)
      ((cons $result $rest)
        (let $program (c0:read-reading $result)
          (if (c0:error? $program) $program
            (if (== $first None) (c0:read-all $rest $program)
              (if (== $program $first) (c0:read-all $rest $first)
                (C0Error "CHR source alternatives have different IR interpretations")))))))))

(= (c0:read $catalogue)
   (case $catalogue
     (((SourceValuesV1) (C0Error "no selected source interpretation"))
      ($values
        (if (== (car-atom $values) SourceValuesV1)
          (c0:read-all (cdr-atom $values) None)
          (C0Error "expected the selected whole-input source catalogue"))))))

(= (c0:declaration-name (C0Declaration $name $arity)) $name)
(= (c0:input-names (C0Program $declarations $rules $inputs))
   (c0:distinct (c0:map c0:declaration-name $declarations) ()))

; ---- Capture ----------------------------------------------------------------

; A captured value: integers, atom names (symbol or string) and tuples.
(= (c0:json-value $json)
   (case $json
     (((JsonObject ((JsonPair "integer" $digits))) (str:string->number $digits))
      ((JsonObject ((JsonPair "symbol" $name))) $name)
      ((JsonObject ((JsonPair "string" $text))) $text)
      ((JsonObject ((JsonPair "expression" (JsonArray $items))))
        (let $fields (c0:map c0:json-value $items)
          (if (c0:error? $fields) $fields (ChrTuple (c0:chr-list $fields)))))
      ($other (C0Error "a C0 constraint argument is an integer, atom name (symbol or string), or tuple")))))

(= (c0:json-name $json)
   (case $json
     (((JsonObject ((JsonPair "symbol" $name))) (Some $name))
      ((JsonObject ((JsonPair "string" $text))) (Some $text))
      ($other None))))

(= (c0:declared-name? $declarations $name $arity) (c0:contains? (C0Declaration $name $arity) $declarations))

(= (c0:value-term $value) (ChrValue $value))

; One captured row: (Captured constraint), NotCaptured, or an error.
(= (c0:capture-row $declarations $row)
   (case $row
     (((JsonObject ((JsonPair "expression" (JsonArray $items))))
        (case $items
          ((() NotCaptured)
           ((cons $head $arguments)
             (case (c0:json-name $head)
               (((Some $name)
                  (if (c0:declared-name? $declarations $name (c0:length $arguments))
                    (let $values (c0:map c0:json-value $arguments)
                      (if (c0:error? $values) $values
                        (Captured (ChrHead $name (c0:length $values) (c0:chr-list (c0:map c0:value-term $values))))))
                    NotCaptured))
                ($other NotCaptured)))))))
      ($other
        (case (c0:json-name $row)
          (((Some $name) (if (c0:declared-name? $declarations $name 0) (Captured (ChrHead $name 0 ChrNil)) NotCaptured))
           ($none NotCaptured)))))))

; Captured rows with their selection ordinals, in capture order.
(= (c0:capture $declarations $rows $ordinal)
   (case $rows
     ((() ())
      ((cons $row $rest)
        (let $read (c0:capture-row $declarations $row)
          (if (c0:error? $read) $read
            (let $tail (c0:capture $declarations $rest (+ $ordinal 1))
              (if (c0:error? $tail) $tail
                (case $read
                  (((Captured $head) (cons-atom (C0Captured $ordinal $head) $tail))
                   (NotCaptured $tail)))))))))))

(= (c0:captured-head (C0Captured $ordinal $head)) $head)
(= (c0:captured-ordinal (C0Captured $ordinal $head)) $ordinal)

; ---- Prepare ----------------------------------------------------------------

; The seeds a run posts: captured occurrences in capture order, then the
; source query in source order.
(= (c0:prepare (C0Program $declarations $rules $inputs) $rows-json)
   (let $rows (case (json:parse $rows-json) (((JsonArray $items) $items) ($other (C0Error "expected an array of captured rows"))))
     (if (c0:error? $rows) $rows
       (let $captured (c0:capture $declarations $rows 0)
         (if (c0:error? $captured) $captured
           (C0Prepared (c0:table $rules)
             (c0:chr-list (c0:append (c0:map c0:captured-head $captured) $inputs))
             (C0Dictionary (c0:map c0:captured-ordinal $captured) (c0:length $inputs))))))))

; ---- Observe ----------------------------------------------------------------

(= (c0:list-items $list)
   (case $list ((ChrNil ()) ((ChrCons $first $rest) (cons-atom $first (c0:list-items $rest))))))

(= (c0:slot-entries (ChrSlot (ChrKey $name $arity) $occurrences))
   (c0:map (c0:occurrence-entry $name) (c0:list-items $occurrences)))
(= (c0:occurrence-entry $name (ChrOcc $id $values)) (C0Entry $id $name (c0:list-items $values)))

(= (c0:entries $slots)
   (case $slots ((() ()) ((cons $slot $rest) (c0:append (c0:slot-entries $slot) (c0:entries $rest))))))

(= (c0:entry-id (C0Entry $id $name $values)) $id)

(= (c0:insert-entry $entry $sorted)
   (case $sorted
     ((() ($entry))
      ((cons $first $rest)
        (if (<= (c0:entry-id $entry) (c0:entry-id $first)) (cons-atom $entry $sorted)
          (cons-atom $first (c0:insert-entry $entry $rest)))))))
(= (c0:sort-entries $entries)
   (case $entries ((() ()) ((cons $first $rest) (c0:insert-entry $first (c0:sort-entries $rest))))))

; Host names: a symbol, or a string for the names no host symbol can hold.
(= (c0:has-nul? $name) (case (str:find $name "\x00") (((TextNotFound $needle) False) ($position True))))
(= (c0:host-name $name)
   (if (== $name "") $name
     (if (== $name "True") $name
       (if (== $name "False") $name
         (if (c0:has-nul? $name) $name (str:string->symbol $name))))))

(= (c0:host-value $value)
   (case $value
     (((ChrTuple $fields) (c0:map c0:host-value (c0:list-items $fields)))
      ($other (if (== (get-type $value) String) (c0:host-name $value) $value)))))
(= (c0:host-constraint (C0Entry $id $name $values))
   (cons-atom (c0:host-name $name) (c0:map c0:host-value $values)))

(= (c0:take $n $items)
   (if (== $n 0) () (case $items ((() ()) ((cons $first $rest) (cons-atom $first (c0:take (- $n 1) $rest)))))))

(= (c0:consumed $pairs $final-ids)
   (case $pairs
     ((() ())
      ((cons (C0Pair $ordinal $id) $rest)
        (if (c0:contains? $id $final-ids) (c0:consumed $rest $final-ids)
          (cons-atom $ordinal (c0:consumed $rest $final-ids)))))))

(= (c0:additions $entries $captured-ids)
   (case $entries
     ((() ())
      ((cons $entry $rest)
        (if (c0:contains? (c0:entry-id $entry) $captured-ids) (c0:additions $rest $captured-ids)
          (cons-atom (c0:host-constraint $entry) (c0:additions $rest $captured-ids)))))))

(= (c0:pair-up $ordinals $ids)
   (case ($ordinals $ids)
     (((() $any) ())
      (((cons $o $os) (cons $i $is)) (cons-atom (C0Pair $o $i) (c0:pair-up $os $is))))))
(= (c0:pair-id (C0Pair $ordinal $id)) $id)

; The observation of a finished run: (C0Observed values consume additions),
; C0Failed, (C0Fault reason) or an error.
(= (c0:observe (C0Dictionary $captured $queries) (ChrMachineResult $status $store $posted))
   (case $status
     ((ChrFailed C0Failed)
      ((ChrFaulted $reason) (C0Fault $reason))
      (ChrComplete
        (let* (($ordered (c0:sort-entries (c0:entries (c0:list-items $store))))
               ($ids (c0:list-items $posted)))
          (if (not (== (c0:length $ids) (+ (c0:length $captured) $queries)))
            (C0Error "C0 run posted an unexpected number of seeds")
            (let* (($pairs (c0:pair-up $captured (c0:take (c0:length $captured) $ids)))
                   ($final-ids (c0:map c0:entry-id $ordered)))
              (C0Observed (c0:map c0:host-constraint $ordered)
                (c0:consumed $pairs $final-ids)
                (c0:additions $ordered (c0:map c0:pair-id $pairs))))))))))

; ---- Entry functions --------------------------------------------------------
; The metta-v1 compiler protocol: each entry answers (LangReplyV1 value) or
; (LangSourceErrorV1 message). The machine runs in the compiler's space.

(= (c0:reply $value) (case $value (((C0Error $message) (LangSourceErrorV1 $message)) ($ok (LangReplyV1 $ok)))))

(= (c0:compiler-read $catalogue) (c0:reply (c0:read $catalogue)))
(= (c0:compiler-inputs $program) (LangReplyV1 (c0:input-names $program)))
(= (c0:compiler-prepare $program $facts)
   (case (c0:prepare $program $facts)
     (((C0Prepared $table $seeds $dictionary)
        (LangReplyV1 (MettaPreparedV1 (gslt:chr-machine:begin $table $seeds) $dictionary)))
      ((C0Error $message) (LangSourceErrorV1 $message)))))
(= (c0:compiler-observe $dictionary $result)
   (case (c0:observe $dictionary $result)
     ((C0Failed (LangReplyV1 MettaFailedV1))
      ((C0Fault $reason) (LangReplyV1 (MettaFaultV1 $reason)))
      ((C0Observed $values $consume $additions) (LangReplyV1 (MettaCompleteV1 $values $consume $additions)))
      ((C0Error $message) (LangSourceErrorV1 $message)))))

; One bounded unit of guest work, and the run's outcome.
(= (lang:compiled:advance $run) (gslt:chr-machine:unit $run 2000))
(= (lang:compiled:outcome $run) (gslt:chr-machine:outcome $run))
