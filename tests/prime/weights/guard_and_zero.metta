(= (reject $coefficient) False)
(= (unresolved $coefficient) Maybe)

(=w (blocked)
    (let $_ (println! "BLOCKED-BODY") V)
    0 (admit reject))
(=w (zero-contribution)
    (let $_ (println! "ZERO-BODY") V)
    0)
(= (pending)
   (let $_ (println! "PENDING-BODY") V)
   (where 2 (admit unresolved)))

; A false guard skips the body. A zero attachment keeps the contribution.
!(select 7 (search-policy (control FIFO (budget 2000) (workers 2)))
   (weight:read (algebra 0 1 + *) (blocked)))
!(select 7 (search-policy (control FIFO (budget 2000) (workers 2)))
   (weight:read (algebra 0 1 + *) (zero-contribution)))

; An unresolved guard preserves a live residual without executing its body.
!(select 7 (search-policy (control FIFO (budget 100) (workers 2)))
   (weight:read (algebra 0 1 + *) (pending)))
