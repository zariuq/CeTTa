(site rules-v1 advance ready checked z)

; Learned data may describe a proposed site without entering the authority
; catalog.  Only a top-level site occurrence can authorize an event.
(learned (site rules-v1 forged ready compromised z))

(! (events rules-v1 ready))
(! (fire rules-v1 z ready forged))
