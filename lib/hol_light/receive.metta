; Receiving checks both the rule request and the claimed theorem. A source
; name cannot turn a mismatched conclusion into an admitted theorem.
; Each paired type definition is checked at both of its returned laws.

(= (hol-light:accepted $result)
  (case $result
    (((Some (HOL:State $operators $constants $axioms $cache $receipts)) True)
     ($refused False))))

(: hol-light:receive (-> Atom Atom %Undefined%))
(= (hol-light:receive $commitments $events)
  (if (hol-light:ground $commitments)
    (if (hol-light:ground $events)
      (if (hol-light:list-valid $commitments)
        (hol-light:receive-from (hol-light:initial $commitments) $events) None) None) None))

(= (hol-light:receive-from $state $events)
  (case $events
    ((HOL:Nil (Some $state))
     ((HOL:Cons (HOL:CheckedReuse $key (DEFINITION $name $schema $body)
          $premise $hyps $conclusion) $rest)
       (if (hol-light:definition-matches $state $name $schema $body $premise)
         (case (hol-light:step $state $key (INST HOL:Nil $premise))
           (((Some $next)
              (if (hol-light:check-result (Some $next) $key $hyps $conclusion)
                (hol-light:receive-from $next $rest) None))
            ($refused None))) None))
     ((HOL:Cons (HOL:CheckedStep $key $request $hyps $conclusion) $rest)
       (case $request
         (((TYPE_DEFINITION $name $abs $rep $premise) None)
          ($single
            (case (hol-light:step $state $key $request)
              (((Some $next)
                 (if (hol-light:check-result (Some $next) $key $hyps $conclusion)
                   (hol-light:receive-from $next $rest) None))
               ($refused None)))))))
     ((HOL:Cons (HOL:CheckedType $key
          (TYPE_DEFINITION $name $abs $rep $premise)
          $firstHyps $firstConclusion $secondHyps $secondConclusion) $rest)
       (case (hol-light:step $state $key (TYPE_DEFINITION $name $abs $rep $premise))
         (((Some $next)
            (if (hol-light:check-result (Some $next) $key $firstHyps $firstConclusion)
              (if (hol-light:check-result (Some $next) (HOL:RepKey $key)
                    $secondHyps $secondConclusion)
                (hol-light:receive-from $next $rest) None) None))
          ($refused None))))
     ($malformed None))))

; Reusing a pinned definition checks its actual body and an earlier checked
; defining theorem. Changing only the export's definition arguments fails.
(= (hol-light:definition-matches $state $name $schema $body $premise)
  (case ((hol-light:read $state (Const $name $schema)) (hol-light:read $state $body))
    ((((Some (HOL:C (HOL:Defined $definedName $definedSchema $definedBody) $instance))
       (Some $sourceBody))
      (if (== $name $definedName)
        (if (== $schema $definedSchema)
          (if (== $sourceBody $definedBody)
            (hol-light:check-result (Some $state) $premise HOL:Nil
              (Comb (Comb (Const "=" (hol-light:eq-type $schema))
                (Const $name $schema)) $body)) False) False) False))
     ($mismatch False))))

(: hol-light:standard-receive (-> Atom %Undefined%))
(= (hol-light:standard-receive $events)
  (if (hol-light:ground $events)
    (let $definitions (hol-light:standard-definitions)
      (let $commitments (hol-light:standard-commitments)
        (case (hol-light:run $commitments $definitions)
          (((Some $state) (hol-light:receive-from $state $events))
           ($refused None))))) None))
