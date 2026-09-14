; Selecting one result after reversing a stream is not first-source-answer
; demand.  All source occurrences remain observable until the order
; transformation has completed.

(= (controller-diversity:reverse-prefix)
   (superpose (first second third)))

!(select 1
   (search-policy recursive-dependent-proof (order reverse))
   (controller-diversity:reverse-prefix))
