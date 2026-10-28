; WIP diagnostic for cached computed reads. Process success alone does not
; establish that the dependent publication was valid.
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
