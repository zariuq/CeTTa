; Qualification of any named proof-relevant relation against List examples.
; Candidate learning remains independent; a language may expose a learned
; program through an ordinary callable relation and reuse this whole layer.

!(import! &self prime_native_mil.metta)
!(import! &self prime_native_list_types.metta)
!(import! &self prime_native_list_relator_types.metta)
!(import! &self prime_native_list_relator.metta)

(= (rel:list-target-witness
     $relation $source-list $target-list)
   (let $proof
        (map-rel:check $relation $source-list $target-list)
     True))

(= (rel:list-positive-covered
     $relation (rel:list-example $source-list $target-list))
   (not (==
     (collapse
       (rel:list-target-witness
         $relation $source-list $target-list))
     ())))

(= (rel:list-negative-avoided
     $relation (rel:list-example $source-list $target-list))
   (==
     (collapse
       (rel:list-target-witness
         $relation $source-list $target-list))
     ()))

(= (rel:list-all-positive $relation ()) True)
(= (rel:list-all-positive $relation $examples)
   (let ($example $rest) (decons-atom $examples)
     (if (rel:list-positive-covered $relation $example)
         (rel:list-all-positive $relation $rest)
         False)))

(= (rel:list-all-negative $relation ()) True)
(= (rel:list-all-negative $relation $examples)
   (let ($example $rest) (decons-atom $examples)
     (if (rel:list-negative-avoided $relation $example)
         (rel:list-all-negative $relation $rest)
         False)))

(= (rel:list-consistent
     $relation (rel:list-examples $positive $negative))
   (if (rel:list-all-positive $relation $positive)
       (rel:list-all-negative $relation $negative)
       False))
