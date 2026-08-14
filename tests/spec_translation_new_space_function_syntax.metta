; Audit: new-space used under let-binding inside a user-defined function body.
; This isolates the actual PeTTa compiler seam from bare-atom space quirks.

(= (use-space)
   (let $s (new-space)
     (let $u1 (add-atom $s (token a))
       (let $u2 (add-atom $s (token b))
         (get-atoms $s)))))

!(use-space)
