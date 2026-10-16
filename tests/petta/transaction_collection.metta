; A transaction's working state survives the heap collections its body
; runs into: what it wrote before a long stretch of work reads back after
; it, and commits or rolls back as it would without the collections.
(= (spin $n)
   (if (== $n 0) done (let $_ (change-state! &spin (pair $n (1 2 3 4))) (spin (- $n 1)))))
!(change-state! &kept 0)
!(transaction
   (let* (($_ (change-state! &kept 41)) ($w (spin 30000)) ($v (get-state &kept)))
     (pair $w $v)))
!(get-state &kept)
!(collapse (transaction
   (let* (($_ (add-atom &self (txn-row 1))) ($w (spin 30000))) (empty))))
!(collapse (match &self (txn-row $x) $x))
!(transaction
   (let* (($_ (add-atom &self (txn-row 2))) ($w (spin 30000))) $w))
!(collapse (match &self (txn-row $x) $x))
!(transaction
   (let* (($a (transaction (let* (($_ (change-state! &nested 7)) ($w (spin 20000))) $w)))
          ($w (spin 20000)))
     (get-state &nested)))
