; PeTTa computed-read diagnostic. The present-space case must retain pending
; work and the insertion. The virtual-location case below remains open.
!(import! &self interaction)
!(import! &self (library lib_memo))
!(bind! &memo-work (new-space))
!(bind! &memo-data (new-space))
!(add-atom &memo-work pending)
!(add-atom &memo-data (item 1))
(= (memo-read-items) (collapse (match &memo-data (item $x) $x)))
!(memoize memo-read-items)
!(memo-read-items)
!(collapse (transform &memo-work
   (, (take pending) (let $xs (memo-read-items))
      (let $_ (add-atom &memo-data (item 2))))
   (add-atom &memo-work done)))
!(collapse (get-atoms &memo-work))
!(collapse (get-atoms &memo-data))

; An absent physical registry entry denotes an empty logical location in
; base PeTTa. Space tokens alone cannot record that lookup's dependency.
(= (memo-virtual-read) (collapse (match &memo-virtual-data marker marker)))
!(memoize memo-virtual-read)
!(memo-virtual-read)
!(add-atom &memo-work virtual-pending)
!(add-atom &memo-virtual-data marker)
!(collapse (transform &memo-work
   (, (take virtual-pending) (let () (memo-virtual-read)))
   (add-atom &memo-work virtual-published)))
!(collapse (get-atoms &memo-work))
