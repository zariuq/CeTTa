;; Rows holding a symbol spelled at run time are terms the universe keeps
;; under ids of their own: the same text parsed again is an equal term with
;; another id, or none.  A ground match answered from the exact-membership
;; index, a membership test and a removal must find such rows by content.
(= (word $n) (parse (format-args "spelled_word_{}" ($n))))
!(bind! &rows (new-space))
!(let $k (word 1) (add-atom &rows (row $k)))
!(let $k (word 1) (add-atom &rows (row $k)))
!(let $k (word 2) (add-atom &rows (row $k)))
!(add-atom &rows (row plain))
!(found-twice (collapse (let $k (word 1) (match &rows (row $k) yes))))
!(found-once (collapse (let $k (word 2) (match &rows (row $k) yes))))
!(absent (collapse (let $k (word 3) (match &rows (row $k) yes))))
!(members (collapse (match &rows (row $w) $w)))
!(contains (let $k (word 2) (_minimal-space-contains-exact &rows (row $k))))
!(contains-absent (let $k (word 3) (_minimal-space-contains-exact &rows (row $k))))
!(let $k (word 1) (remove-atom &rows (row $k)))
!(after-one-removal (collapse (let $k (word 1) (match &rows (row $k) yes))))
!(let $k (word 1) (remove-atom &rows (row $k)))
!(after-two-removals (collapse (let $k (word 1) (match &rows (row $k) yes))))
!(still-there (collapse (let $k (word 2) (match &rows (row $k) yes))))
