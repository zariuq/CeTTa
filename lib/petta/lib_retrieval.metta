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

; Inspect each candidate once, collecting only failures. Rebuilding the full
; expression with recursive cons-atom copies every growing tail. Returning the
; original successful view preserves its occurrences without that extra copy.
(= (retrieval:failure $candidate)
   (case $candidate
     (((RetrievalFailure $id $reason) (RetrievalFailure $id $reason))
      (($score $hit) (empty))
      ($invalid (RetrievalFailure $invalid malformed-candidate)))))
(= (retrieval:validate $scored)
   (let $failures (collapse (retrieval:failure (superpose $scored)))
     (if (== $failures ()) $scored (car-atom $failures))))

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
