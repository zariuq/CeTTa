; Under petta@1.1.c a failed contract check that no catch takes ends the
; run with PeTTa's message.
(: num-id (-> Number Number))
(= (num-id $x) $x)
!(num-id wrong)
