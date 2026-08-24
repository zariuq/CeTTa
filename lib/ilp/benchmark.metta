; Deterministic accounting for finite MIL candidate bags.
;
; Candidate collections are bags: every occurrence matters.  The accounting
; therefore removes one matching occurrence at a time instead of converting a
; producer result to a set.  Callers construct the tagged trace directly at
; the candidate producer boundary, where the staged checker receives each raw
; program before it is materialized as an expression-valued list.

!(import! &self ../list.metta)

; Atom arguments are staged by Prime.  These declarations let a bound program
; be substituted into an accounting record without evaluating the hypothesis.
(: MIL.CandidateData (-> Atom Atom))
(: MIL.CandidateVerdict (-> Atom Atom Atom Atom))

(= (mil:bench:same-atom $left $right)
   (== (quote $left) (quote $right)))

; Remove exactly one occurrence.  The result records whether removal happened
; so absence cannot be confused with removal from a singleton bag.
(= (mil:bench:bag-remove-first $needle ())
   (MIL.BagRemove False ()))
(= (mil:bench:bag-remove-first $needle $items)
   (let ($head $tail) (decons-atom $items)
     (if (mil:bench:same-atom $needle $head)
         (MIL.BagRemove True $tail)
         (let (MIL.BagRemove $found $rest)
              (mil:bench:bag-remove-first $needle $tail)
           (MIL.BagRemove $found (cons-atom $head $rest))))))

(= (mil:bench:bag-equal () $right)
   (list:is-empty $right))
(= (mil:bench:bag-equal $left $right)
   (let ($head $tail) (decons-atom $left)
     (let (MIL.BagRemove $found $rest)
          (mil:bench:bag-remove-first $head $right)
       (if $found
           (mil:bench:bag-equal $tail $rest)
           False))))

; A set-based implementation would accept the second comparison.  Keeping
; both observations together makes multiplicity part of the executable gate.
(= (mil:bench:bag-canaries)
   (and
     (mil:bench:bag-equal
       ((MIL.CandidateData alpha)
        (MIL.CandidateData beta)
        (MIL.CandidateData alpha))
       ((MIL.CandidateData alpha)
        (MIL.CandidateData alpha)
        (MIL.CandidateData beta)))
     (not
       (mil:bench:bag-equal
         ((MIL.CandidateData alpha) (MIL.CandidateData alpha))
         ((MIL.CandidateData alpha))))))

(= (mil:bench:erase-type-trace ()) ())
(= (mil:bench:erase-type-trace $trace)
   (let ((MIL.CandidateVerdict $status $example-status $program) $rest)
        (decons-atom $trace)
     (let $tail (mil:bench:erase-type-trace $rest)
       (cons-atom (MIL.CandidateData $program) $tail))))

(= (mil:bench:status-bag () $wanted) ())
(= (mil:bench:status-bag $trace $wanted)
   (let ((MIL.CandidateVerdict $status $example-status $program) $rest)
        (decons-atom $trace)
     (let $tail (mil:bench:status-bag $rest $wanted)
       (if (== $status $wanted)
           (cons-atom (MIL.CandidateData $program) $tail)
           $tail))))

; Safe pruning removes only checked refutations.  Established and
; Undetermined occurrences both stay in the frontier.
(= (mil:bench:safe-frontier ()) ())
(= (mil:bench:safe-frontier $trace)
   (let ((MIL.CandidateVerdict $status $example-status $program) $rest)
        (decons-atom $trace)
     (let $tail (mil:bench:safe-frontier $rest)
       (if (== $status Refuted)
           $tail
           (cons-atom (MIL.CandidateData $program) $tail)))))

; One stable record is both human-readable benchmark output and a deterministic
; gate.  Timing and peak memory deliberately do not belong in this record.
(= (mil:bench:summary
     $name $mode $generated $typed $trace $example-consistent)
   (let* (($established
             (mil:bench:status-bag $trace Established))
          ($refuted
             (mil:bench:status-bag $trace Refuted))
          ($undetermined
             (mil:bench:status-bag $trace Undetermined))
          ($erased
             (mil:bench:erase-type-trace $trace))
          ($safe
             (mil:bench:safe-frontier $trace))
          ($generated-count (list:len $generated))
          ($established-count (list:len $established))
          ($refuted-count (list:len $refuted))
          ($undetermined-count (list:len $undetermined)))
     (MIL.BenchV1
       (Name $name)
       (Mode $mode)
       (CandidatesGenerated $generated-count)
       (CandidatesChecked (list:len $trace))
       (Established $established-count)
       (Refuted $refuted-count)
       (Undetermined $undetermined-count)
       (Incomplete 0)
       (SafeFrontier (list:len $safe))
       (ExampleChecked (list:len $safe))
       (ExampleConsistent (list:len $example-consistent))
       (CountConserved
         (== $generated-count
             (+ $established-count
                (+ $refuted-count $undetermined-count))))
       (TraceConserved
         (mil:bench:bag-equal $generated $erased))
       (TypedProducerEqualsSafeFrontier
         (mil:bench:bag-equal $typed $safe)))))
