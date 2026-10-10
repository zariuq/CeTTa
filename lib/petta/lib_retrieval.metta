; Exact retrieval over an explicit finite record view.
; Records: (Embedding id revision profile tensor metadata).
; The space owns records; this library derives scores without storing a second
; authoritative copy. Profile selection and source records remain visible.
!(import! &self (library lib_tensor))

(= (retrieval:score-dot $query (Embedding $id $revision $profile $vector $metadata))
   (case (tensor:dot $query $vector)
     (((TensorFailure $reason) (RetrievalFailure $id $reason))
      ($score
       (if (or (isnan-math $score) (isinf-math $score))
           (RetrievalFailure $id nonfinite-score)
           ($score (RetrievalHit $id $revision $profile $metadata)))))))

; Validation stops at the first failed candidate instead of silently claiming
; a complete search over only the successful subset.
(= (retrieval:validate ()) ())
(= (retrieval:validate (cons (RetrievalFailure $id $reason) $rest))
   (RetrievalFailure $id $reason))
(= (retrieval:validate (cons ($score $hit) $rest))
   (case (retrieval:validate $rest)
     (((RetrievalFailure $id $reason) (RetrievalFailure $id $reason))
      ($valid (cons-atom ($score $hit) $valid)))))

; The native selector keeps the greatest k scores, preserves occurrence order,
; and prefers earlier occurrences at a boundary tie. It does not sort by score.
(= (retrieval:exact-dot $query $records $k)
   (case (retrieval:validate
           (collapse (retrieval:score-dot $query (superpose $records))))
     (((RetrievalFailure $id $reason) (RetrievalFailure $id $reason))
      ($scored (retain-top-k-keyed-atom $scored $k)))))

(= (retrieval:search-dot $space $profile $query $k)
   (let $records
     (collapse (match $space
       (Embedding $id $revision $profile $vector $metadata)
       (Embedding $id $revision $profile $vector $metadata)))
     (retrieval:exact-dot $query $records $k)))
