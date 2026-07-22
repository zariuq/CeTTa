; Runtime half of a typed state-effect example.  Annotation erasure must not
; change either the receipt-local writes or the ambient observations around
; them.  nop is an explicit effect sink, so it demands its argument under
; Prime Need; without an explicit commit policy, the demanded write remains in
; its branch receipt and does not mutate the ambient cell.

(: prime-grad:read (-> (StateMonad Number) Number))
(: prime-grad:increment! (-> (StateMonad Number) (StateMonad Number)))

(= (prime-grad:read $state) (get-state $state))
(= (prime-grad:increment! $state)
   (change-state! $state (+ (get-state $state) 1)))

!(bind! &prime-grad:counter (new-state 40))
!(prime-grad:read &prime-grad:counter)
!(nop (prime-grad:increment! &prime-grad:counter))
!(prime-grad:read &prime-grad:counter)
!(nop (prime-grad:increment! &prime-grad:counter))
!(prime-grad:read &prime-grad:counter)
