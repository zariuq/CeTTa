; Checked incremental decomposition in ordinary PeTTa.
; The common algorithm is Algorithms/Compression/Checked.lean. Search proposes
; features; this library independently checks reconstruction and strict cost.
; Concrete families supply extract/expand/cost/propose operations separately.
; A declared cost is not automatically a prefix-code or optimality claim.
; No Python, Prolog, native learner opcode, or WILLIAM implementation is used.

(= (ic:nat-value $value)
   (case $value
     ((IC:Z (IC:Some 0))
      ((IC:S $previous)
       (case (ic:nat-value $previous)
         (((IC:Some $number) (IC:Some (+ $number 1)))
          ($other IC:None))))
      ($other IC:None))))

(= (ic-internal:nat $number)
   (if (<= $number 0) IC:Z
     (let $previous (ic-internal:nat (- $number 1)) (IC:S $previous))))

; Kernel extensions supply deterministic, pure extract/expand/cost operations
; with the IC:Some/IC:None boundary. The host-independent proof applies to
; implementations of those operations, not to arbitrary effectful callbacks.
(= (ic:check $kernel $source $feature)
   (case (ic:extract $kernel $feature $source)
     (((IC:Some $residual)
       (let* (($expanded (ic:expand $kernel $feature $residual))
              ($fee (ic:feature-cost $kernel $feature))
              ($after (ic:data-cost $kernel $residual))
              ($before (ic:data-cost $kernel $source)))
         (case ($expanded $fee $after $before)
           ((((IC:Some $restored) (IC:Some $f) (IC:Some $r) (IC:Some $s))
             (if (and (== $restored $source) (< (+ $f $r) $s))
               (IC:Accepted $feature $residual)
               IC:Rejected))
            ($other IC:Rejected)))))
      (IC:None IC:Rejected)
      ($other (IC:Unavailable Extract)))))

(= (ic:select $kernel $source $proposals)
   (if (== $proposals ()) IC:None
     (let ($feature $rest) (decons-atom $proposals)
       (case (ic:check $kernel $source $feature)
         (((IC:Accepted $chosen $residual) (IC:Accepted $chosen $residual))
          (IC:Rejected (ic:select $kernel $source $rest))
          ($other $other))))))

(= (ic:compress $kernel $proposals $fuel $source)
   (ic:compress-using $kernel (IC:Fixed $proposals) $fuel $source))

(= (ic:compress-generated $kernel $fuel $source)
   (ic:compress-using $kernel IC:Generated $fuel $source))

(= (ic:proposals-for $policy $kernel $source)
   (case $policy
     (((IC:Fixed $proposals) $proposals)
      (IC:Generated (ic:propose $kernel $source)))))

(= (ic:compress-using $kernel $policy $fuel $source)
   (case (ic:nat-value $fuel)
     (((IC:Some $budget)
       (case (ic:data-cost $kernel $source)
         (((IC:Some $cost) (ic:compress-loop $kernel $policy $budget $source))
          (IC:None (IC:Invalid Source))
          ($other (IC:Unavailable DataCost)))))
      ($other (IC:Invalid Fuel)))))

(= (ic:compress-loop $kernel $policy $budget $source)
   (if (== $budget 0) (IC:Plan () $source IC:BudgetExhausted)
     (let $proposals (ic:proposals-for $policy $kernel $source)
      (case (ic:select $kernel $source $proposals)
       (((IC:Accepted $feature $residual)
         (case (ic:compress-loop $kernel $policy (- $budget 1) $residual)
           (((IC:Plan $tail $last $stop)
             (let $features (cons-atom $feature $tail) (IC:Plan $features $last $stop)))
            ($other $other))))
        (IC:None (IC:Plan () $source IC:NoCandidateAccepted))
        ($other $other))))))

(= (ic:restore $kernel $features $residual)
   (if (== $features ()) (IC:Some $residual)
     (let ($feature $rest) (decons-atom $features)
       (case (ic:restore $kernel $rest $residual)
         (((IC:Some $inner) (ic:expand $kernel $feature $inner))
          ($other $other))))))

(= (ic:plan-cost $kernel $features $residual)
   (if (== $features ()) (ic:data-cost $kernel $residual)
     (let ($feature $rest) (decons-atom $features)
       (let* (($fee (ic:feature-cost $kernel $feature))
              ($tail (ic:plan-cost $kernel $rest $residual)))
         (case ($fee $tail)
           ((((IC:Some $first) (IC:Some $remaining)) (IC:Some (+ $first $remaining)))
            ($other IC:None)))))))
