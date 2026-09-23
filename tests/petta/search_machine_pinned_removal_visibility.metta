; A match in progress keeps draining the occurrences it selected, even when
; its continuation removes them.  Every other observation made in that
; continuation sees only the survivors: a later removal, a listing of the
; Space, a relational call, and a fresh match.

; Removing the second occurrence again matches nothing, which is the
; identity, not a failure.
!(add-atom &pinned-duplicates (row same))
!(add-atom &pinned-duplicates (row same))
!(let $value (match &pinned-duplicates (row $_) (row $_))
      (let $removed (remove-atom &pinned-duplicates $value) (removed $removed)))
!(test (collapse (get-atoms &pinned-duplicates)) ())

; The listing inside the continuation omits the removed rows.
!(add-atom &pinned-listing (row same))
!(add-atom &pinned-listing (row same))
!(add-atom &pinned-listing (other keep))
!(let $value (match &pinned-listing (row $_) (row $_))
      (let $removed (remove-atom &pinned-listing $value)
           (seen (collapse (get-atoms &pinned-listing)))))

; A removed row that is added back appears once.
!(add-atom &pinned-readd (row a))
!(let $value (match &pinned-readd (row $x) $x)
      (let $removed (remove-atom &pinned-readd (row a))
           (let $added (add-atom &pinned-readd (row a))
                (listed (collapse (get-atoms &pinned-readd))))))

; A removed equation takes no part in a later call.
(= (pinned-choice) one)
(= (pinned-choice) two)
(pinned-trigger go)
!(let $value (match &self (pinned-trigger $x) $x)
      (let $removed (remove-atom &self (= (pinned-choice) one))
           (answers (collapse (pinned-choice)))))

; The selected occurrences are still drained, in order, while a fresh
; match sees only what survives.
!(add-atom &pinned-drain (item 1))
!(add-atom &pinned-drain (item 2))
!(add-atom &pinned-drain (item 3))
!(let $n (match &pinned-drain (item $k) $k)
      (let $removed (remove-atom &pinned-drain (item $_))
           (drained $n (collapse (match &pinned-drain (item $m) $m)))))
