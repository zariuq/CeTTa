; Finite mixed parameter regions over immutable packed vectors. Search owns
; each cooperative continuation; later choices are not collapsed or discarded.
!(import! &self (library tensor))
!(import! &self "lib/petta/lib_ic_search.metta")

(= (ic-parameters:vector? $dtype $value)
   (if (tensor:is-tensor $value)
     (let $shape (tensor:shape $value)
       (if (and (== (tensor:dtype $value) $dtype) (== (size-atom $shape) 1))
         (let (TensorEntries $keys $values) (tensor:entries $value)
           (and (== (tensor:shape $values) $shape) (tensor:all-finite $value)))
         False)) False))

(= (ic-parameters:domain? $domain)
   (if (tensor:is-tensor $domain)
     (or (ic-parameters:vector? i64 $domain) (ic-parameters:vector? f64 $domain)) False))
(= (ic-parameters:valid? $domains)
   (if (== (get-metatype $domains) Expression)
     (if (== $domains ()) True
       (let ($head $tail) (decons-atom $domains)
         (and (ic-parameters:domain? $head) (ic-parameters:valid? $tail)))) False))

; Snapshot only each finite parameter domain, not its Cartesian product or
; the numerical observations. Native reads finish before retaining a producer.
; Logical vector positions, including duplicates, determine enumeration order.
(= (ic-parameters:snapshot-values $domain $kind $index $count)
   (if (== $index $count) ()
     (let* (((tensor:present $value) (tensor:at $domain $index))
            ($choice (case $kind
               ((i64 (IC:IntegerParameter $value)) (f64 (IC:FloatParameter $value)))))
            ($rest (ic-parameters:snapshot-values $domain $kind (+ $index 1) $count)))
       (cons-atom $choice $rest))))
(= (ic-parameters:snapshot $domains)
   (if (== $domains ()) ()
     (let* ((($head $tail) (decons-atom $domains))
            ($choices (ic-parameters:snapshot-values $head (tensor:dtype $head) 0
                        (index-atom (tensor:shape $head) 0)))
            ($rest (ic-parameters:snapshot $tail)))
       (cons-atom $choices $rest))))
(= (ic-parameters:prepare $domains)
   (if (ic-parameters:valid? $domains)
     (let $choices (ic-parameters:snapshot $domains) (IC:ParameterRegion $choices))
     (IC:ParameterFault InvalidDomains)))

; One explicit frontier gives the same product order under direct evaluation
; and retained FIFO execution. Recursive relational cross-products alone would
; let the host scheduler interleave different dimensions diagonally.
(= (ic-parameters:children $chosen $choices $remaining)
   (if (== $choices ()) ()
     (let* ((($head $tail) (decons-atom $choices))
            ($assignment (union-atom $chosen (cons-atom $head ())))
            ($rest (ic-parameters:children $chosen $tail $remaining)))
       (cons-atom (IC:ParameterFrame $assignment $remaining) $rest))))
(= (ic-parameters:tick $frontier)
   (if (== $frontier ()) IC:ParameterDone
     (let ($frame $rest) (decons-atom $frontier)
       (case $frame
         (((IC:ParameterFrame $chosen $domains)
           (if (== $domains ()) (IC:ParameterYield $chosen $rest)
             (let* ((($choices $remaining) (decons-atom $domains))
                    ($children (ic-parameters:children $chosen $choices $remaining))
                    ($next (union-atom $children $rest)))
               (IC:ParameterPending $next))))
          ($other (IC:ParameterFault InvalidFrontier)))))))
(= (ic-parameters:walk $frontier)
   (case (ic-parameters:tick $frontier)
     (((IC:ParameterYield $assignment $next) (ic-parameters:emit $assignment $next))
      ((IC:ParameterPending $next) (ic-parameters:walk $next))
      (IC:ParameterDone (superpose ()))
      ($other $other))))
(= (ic-parameters:emit $assignment $next) $assignment)
(= (ic-parameters:emit $assignment $next) (ic-parameters:walk $next))
(= (ic-parameters:enumerate $domains)
   (let $frontier (cons-atom (IC:ParameterFrame () $domains) ())
     (ic-parameters:walk $frontier)))
(= (ic-parameters:proposals $domains)
   (let $parameters (ic-parameters:enumerate $domains) (IC:Proposal (IC:ParameterProposal $parameters))))

; Integer choices cross an exact typed conversion boundary. A large integer
; that binary64 cannot represent is refused, rather than rounded implicitly.
(= (ic-parameters:float $parameter)
   (case $parameter
     (((IC:FloatParameter $value)
       (case (tensor:from-values f64 (1) ($value))
         (((Tensor:Fault $reason) (IC:ParameterFault $reason))
          ($converted
            (if (tensor:all-finite $converted)
              (let (tensor:present $result) (tensor:at $converted 0) (IC:ParameterValue $result))
              (IC:ParameterFault NonFinite))))))
      ((IC:IntegerParameter $value)
        (case (tensor:from-values i64 (1) ($value))
          (((Tensor:Fault $reason) (IC:ParameterFault $reason))
           ($integer
             (case (tensor:cast $integer f64 exact)
               (((Tensor:Fault $reason) (IC:ParameterFault $reason))
                ($converted
                  (let (tensor:present $result) (tensor:at $converted 0) (IC:ParameterValue $result)))))))))
      ($other (IC:ParameterFault WrongParameterSort)))))
