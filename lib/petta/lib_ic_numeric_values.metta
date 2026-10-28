; Scalar observation shared by vector and arbitrary-rank numeric tasks.
; A failed subtraction/comparison rejects an observed coordinate; masked
; coordinates perform no arithmetic. This is not approximate key equality.
!(import! &self (library lib_binary_value))

; One constructor for encoded-program proposals in both numerical protocols.
; This constructs a candidate, not a certificate: the task's normal observer
; still decodes and executes the program before accepting its complete cost.
(= (ic-numeric:program-candidate $domain $original $program)
   (if (binary-tree:bytes? $program)
     (let $decoded (binary-value:unpack $original)
      (case (Numeric.ProgramContext $domain $decoded)
       (((Numeric.ProgramContext IC:NumericArchive
            (BinaryValue:Value (Numeric:Archive "NC1" $inputs $mask $precision $body)))
         (case (binary-value:pack (Numeric:Archive "NC1" $inputs $mask $precision (Numeric:Program $program)))
           (((Binary:TreeBytes $bytes) (IC:Candidate $bytes)) ($bad-vector-codec (IC:ProducerFault Codec)))))
        ((Numeric.ProgramContext IC:ArrayArchive
            (BinaryValue:Value (Numeric:ArrayArchive "NC2" $inputs $sort $mask $criterion $body)))
         (case (binary-value:pack
                 (Numeric:ArrayArchive "NC2" $inputs $sort $mask $criterion (Numeric:Program $program)))
           (((Binary:TreeBytes $bytes) (IC:Candidate $bytes)) ($bad-array-codec (IC:ProducerFault Codec)))))
        ($invalid-original (IC:ProducerFault Original)))))
     (IC:Refused InvalidProgram)))

(= (ic-numeric:near $tolerance $mask $expected $actual)
   (if $mask
     (case (catch (and (<= (- $actual $expected) $tolerance)
                      (<= (- $expected $actual) $tolerance)))
       (((Error $reason $context) False) ($decision $decision))) True))
