; Judgments reused across a program revision depend on every declaration
; typing reads.  A subtype edge is one; HE reads it ((: sx SA) then
; (:< SA SB) gives (get-type sx) [SA, SB]).  Each mode answers as per-call
; checking does, whatever the engine reads.
(: sx SA)
!(get-type sx)
!(add-atom &self (:< SA SB))
!(get-type sx)
!(remove-atom &self (:< SA SB))
!(get-type sx)
; get-type gives an untyped call one evaluation; that is not a judgment, and
; its body runs at each query in every mode.
(= (noisy) (let $_ (println! noisy-ran) 1))
!(get-type (noisy))
!(add-atom &self (unrelated data))
!(get-type (noisy))
