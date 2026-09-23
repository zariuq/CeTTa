; Live reads and saved values.  A saved address names a live space; a value
; once obtained does not change; a new read may see an update; an explicit
; snapshot is a version captured on request; scope exit is not rollback.
!(bind! &B (new-space))
!(add-atom &B (stock 23))
!(add-atom &B (rev 1))
; 1. Save a read, update the space, read again: saved 23, fresh 99.
!(let $saved (match &B (stock $n) $n)
   (let $_ (remove-atom &B (stock 23))
     (let $_ (add-atom &B (stock 99))
       (Pair $saved (match &B (stock $m) $m)))))
; 2. A snapshot captured on request keeps the version it saw.
!(with-space-snapshot $v &B
   (let $_ (remove-atom &B (stock 99))
     (let $_ (add-atom &B (stock 7))
       (Pair (match $v (stock $n) $n) (match &B (stock $m) $m)))))
; 3. Compound read across an interleaved writer: per-read observation sees
;    the pair (7 . 2) with stock from before and rev from after the write.
(= (writer) (let $_ (remove-atom &B (stock 7)) (let $_ (add-atom &B (stock 8)) (let $_ (remove-atom &B (rev 1)) (add-atom &B (rev 2))))))
!(let $s (match &B (stock $n) $n) (let $_ (writer) (Pair $s (match &B (rev $r) $r))))
; 4. The same compound read under a snapshot is revision-consistent: (8 . 2).
!(with-space-snapshot $v &B (Pair (match $v (stock $n) $n) (match $v (rev $r) $r)))
; 5. A write inside an explicit-space evaluation survives leaving it.
!(bind! &F (new-space))
!(eval (add-atom &self (mark left)) &F)
!(match &F (mark $m) $m)
!(match &self (mark $m) $m)
