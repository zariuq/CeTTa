; Cached computed sources retain the original Space reads and effects.
!(import! &self interaction)
!(import! &self (library lib_memo))
!(bind! &mir-work (new-space))
!(bind! &mir-data (new-space))
!(bind! &mir-effects (new-space))
!(add-atom &mir-data (item 1))
(= (mir-items)
   (let $items (collapse (match &mir-data (item $x) $x))
     (let $_ (add-atom &mir-effects evaluated) $items)))
!(memoize mir-items)
!(test (mir-items) (1))
!(test (collapse (get-atoms &mir-effects)) (evaluated))

; Replaying a current answer does not run its effects again.
!(add-atom &mir-work good)
!(test (collapse (transform &mir-work
   (, (take good) (let (1) (mir-items)))
   (add-atom &mir-work accepted))) (()))
!(test (collapse (get-atoms &mir-work)) (accepted))
!(test (collapse (get-atoms &mir-effects)) (evaluated))

; A later write and fresh read cannot replace the cached read certificate.
!(add-atom &mir-work stale)
!(test (collapse (transform &mir-work
   (, (take stale) (let (1) (mir-items))
      (let $_ (add-atom &mir-data (item 2)))
      (let $fresh (collapse (match &mir-data (item $x) $x))))
   (add-atom &mir-work forbidden))) ())
!(test (collapse (match &mir-work stale stale)) (stale))
!(test (collapse (match &mir-work forbidden forbidden)) ())
!(test (sort-atom (collapse (get-atoms &mir-data))) ((item 1) (item 2)))
!(test (collapse (get-atoms &mir-effects)) (evaluated))

; A cached certificate already stale on entry still cannot publish.
!(test (collapse (transform &mir-work
   (, (take stale) (let (1) (mir-items)))
   (add-atom &mir-work forbidden))) ())
!(test (collapse (match &mir-work stale stale)) (stale))
!(test (collapse (get-atoms &mir-effects)) (evaluated))

; Clearing the cache permits a fresh computation and publication.
!(clear-memoize)
!(test (collapse (transform &mir-work
   (, (take stale) (let $items (mir-items)))
   (add-atom &mir-work (fresh $items)))) (()))
!(test (collapse (match &mir-work (fresh $items) $items)) ((1 2)))
!(test (collapse (get-atoms &mir-effects)) (evaluated evaluated))

; An empty answer bag over a present space carries a read dependency.
!(bind! &mir-empty (new-space))
!(add-atom &mir-empty unrelated)
(= (mir-missing) (collapse (match &mir-empty marker marker)))
!(memoize mir-missing)
!(test (mir-missing) ())
!(add-atom &mir-work negative)
!(add-atom &mir-empty marker)
!(test (collapse (transform &mir-work
   (, (take negative) (let () (mir-missing)))
   (add-atom &mir-work forbidden))) ())
!(test (collapse (match &mir-work negative negative)) (negative))
!(test (collapse (match &mir-work forbidden forbidden)) ())
