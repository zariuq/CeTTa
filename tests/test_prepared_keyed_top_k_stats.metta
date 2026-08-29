!(import! &self list)

(= (prepared-top-k:key (item $key $value))
   $key)

!(assertEqual
  (list:retain-top-k-by-number prepared-top-k:key
    ((item 2 first) (item 3 second) (item 1 third) (item 3 fourth)) 3)
  ((item 2 first) (item 3 second) (item 3 fourth)))
!(assertEqual
  (list:retain-top-k-by-number prepared-top-k:key
    ((item 2 first) (item 3 second)) 0)
  ())
!(assertEqual
  (list:retain-top-k-by-number prepared-top-k:key
    ((item 2 first) (item 3 second)) 7)
  ((item 2 first) (item 3 second)))
!(assertEqual
  (list:retain-top-k-by-number prepared-top-k:key
    ((item 3 first) (item 3 second) (item 3 third)) 2)
  ((item 3 first) (item 3 second)))

; Overlapping key clauses force the complete relational fallback.  Every
; ordinary key occurrence, including equal result occurrences, is retained.
(= (prepared-top-k:choice-key (item $key $value))
   $key)
(= (prepared-top-k:choice-key (item $key $value))
   (+ $key 10))
!(assertEqualToResult
  (list:retain-top-k-by-number prepared-top-k:choice-key
    ((item 2 a) (item 3 b)) 1)
  (((item 3 b)) ((item 2 a)) ((item 3 b)) ((item 3 b))))

; A pure but nonnumeric key may be prepared, but cannot authorize numeric
; selection.  The ordinary fallback remains responsible for its exact fault.
(= (prepared-top-k:bad-key $item)
   nope)
!(list:retain-top-k-by-number prepared-top-k:bad-key (a b) 1)
