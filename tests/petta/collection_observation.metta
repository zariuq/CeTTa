!(import! &self (library list))

; A space query retains occurrences. Equal keys do not imply replacement.
!(let* (($s (new-space))
        ($added (add-atoms $s ((Entry k 7) (Entry k 7) (Entry k 9) (Entry q 4))))
        ($rows (sort-atom (collapse (match $s (Entry k $value) $value)))))
   (Occurrences $rows (size-atom $rows) (unique-atom $rows)))

; A captured value is a snapshot; observing the source again sees mutation.
!(let* (($s (new-space)) ($added (add-atom $s (Item a)))
        ($snapshot (collapse (match $s (Item $x) $x)))
        ($changed (add-atom $s (Item b))))
   (Snapshot $snapshot (sort-atom (collapse (match $s (Item $x) $x)))))

; Deduplication is explicit and changes additive evidence.
!(let* (($rows ((k 2) (k 2)))
        ($distinct (unique-atom $rows)))
   (Evidence (list:group-sums-number $rows)
             (list:group-sums-number $distinct)))

; Binary64 addition does not descend to an unordered bag fold.
!(Schedule
   (foldl-atom (10000000000000000.0 1.0 -10000000000000000.0) 0.0 $s $x (+ $s $x))
   (foldl-atom (10000000000000000.0 -10000000000000000.0 1.0) 0.0 $s $x (+ $s $x)))

; One ordinary traversal can keep two independent non-associative schedules.
!(foldl-atom (2 5) (Summary 0 0) $state $x
   (let (Summary $count $digits) $state
     (Summary (+ $count 1) (+ (* $digits 10) $x))))
