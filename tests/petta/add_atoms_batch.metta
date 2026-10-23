; The shared add-atoms vocabulary is native in CeTTa's PeTTa lane. A data
; block publishes once while retaining source order and duplicate occurrences.
!(let $space (new-space)
   (let $result (add-atoms $space ((row 1) (row 1) (row 2)))
     (BatchValueV1 $result
       (size-atom (collapse (get-atoms $space)))
       (size-atom (collapse (match $space (row 1) hit)))
       (collapse (match $space (row $value) $value)))))

; A named space is created with the same policy as add-atom.
!(add-atoms &batch-named ((named a) (named b)))
!(size-atom (collapse (match &batch-named (named $value) $value)))

; Equations/declarations retain the sequential program-catalog observer.
!(add-atoms &self ((= (batch-added $value) (+ $value 1))))
!(batch-added 4)

; The batch boundary observes list elements, not a native carrier's tag.
!(import! &self (library list))
!(let* (($space (new-space)) ($rows (list:from-expression (a b a)))
        ($stored (add-atoms $space $rows)))
   (LogicalBatch (size-atom (collapse (get-atoms $space))) (collapse (get-atoms $space))))
!(let* (($space (new-space)) ($rows (cons a (cons b ())))
        ($stored (add-atoms $space $rows)))
   (LogicalBatch (size-atom (collapse (get-atoms $space))) (collapse (get-atoms $space))))
