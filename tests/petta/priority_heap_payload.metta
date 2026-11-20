; A heap's value is payload.  The heap stores it as written, open or
; rational, and take returns it with its variables, which stay variables of
; the program.  heap:insert and the imported add_to_heap build the same heap
; from the same calls.  A priority is ordered by the finite term ordering, so
; it stays ground and finite: an open priority leaves heap:insert
; unevaluated, and the imported add_to_heap orders it as the Prolog library
; does.
!(import! &self (library heap))
!(println! (ground (heap:size (heap:insert (heap:empty) 1.0 (item a)))))
!(println! (nonground (heap:insert (heap:empty) 1.0 (item $x))))
!(use-module! heaps)
!(import_prolog_function (superpose (empty_heap add_to_heap get_from_heap heap_size)))
!(println! (prolog-iface-nonground (add_to_heap (empty_heap) 1.0 (item $x))))

(= (heap-drain $heap)
   (case (heap:take $heap)
     ((Heap:Empty ())
      ((Heap:Item $key $value $rest)
       (let $tail (heap-drain $rest) (cons-atom ($key $value) $tail))))))

!(println!
  (Agree (== (heap:insert (heap:empty) 1.0 (item $x))
             (add_to_heap (empty_heap) 1.0 (item $x)))))
!(println!
  (let* (($native
           (heap:insert
             (heap:insert (heap:insert (heap:empty) 1 (early $x)) 0 (pair $y $y))
             1 (late $z)))
         ($imported
           (add_to_heap
             (add_to_heap (add_to_heap (empty_heap) 1 (early $x)) 0 (pair $y $y))
             1 (late $z))))
    (AgreeMany (== $native $imported) (heap:size $native)
      (heap:valid? $native) (heap-drain $native))))
!(println!
  (let* (($heap (heap:insert (heap:empty) 2 (item $x)))
         ((Heap:Item $key $value $rest) (heap:take $heap))
         ($x bound))
    (TakeShares $key $value (heap:size $rest))))
!(println!
  (let* (($heap (add_to_heap (empty_heap) 2 (item $x)))
         ($rest (get_from_heap $heap $key $value))
         ($x bound))
    (GetShares $key $value (heap_size $rest))))
!(println!
  (let $_ (translatePredicate (= $c (f $c)))
    (let* (($heap (heap:insert (heap:empty) 1 $c))
           ((Heap:Item $key $value $rest) (heap:take $heap)))
      (RationalValue (== $value $c) (heap:size $heap) (heap:valid? $heap)))))
!(println! (OpenPriority (heap:insert (heap:empty) $k v)))
!(println! (OpenPriorityImported (add_to_heap (empty_heap) $k v)))
!(println! (OpenPriorityValid (heap:valid? (heap (t v $k ()) 1))))
