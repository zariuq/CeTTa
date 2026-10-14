; A first set insertion creates the named space, then suppresses duplicates.
!(add-atom-nodup &first-set (pair a b))
!(add-atom-nodup &first-set (pair a b))
!(test (collapse (match &first-set (pair $x $y) (pair $x $y))) ((pair a b)))
(= (insert-once $space $row) (add-atom-nodup $space $row))
!(insert-once &parameter-set (pair c d))
!(insert-once &parameter-set (pair c d))
!(test (collapse (match &parameter-set $x $x)) ((pair c d)))

; Invalid references are not implicitly replaced by spaces.
!(remove-atom 7 (item 1))
!(add-atom-nodup 7 (item 1))
