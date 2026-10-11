; Observe semantic search outcomes and independently recheck every retained proof.
; Work counters and fresh allocation names are not the result of the search.
(= (native-search:observe $space
      (typing-search $state $depth $reason $proofs $stats))
   (let $entries (cdr-atom $proofs)
     (Search $state $depth $reason
       (map-atom $entries $answer
         (let (typed-answer-v2 (: $term $type) $substitution) $answer
           (Checked $term $type (type:check $space $term $type)))))))
