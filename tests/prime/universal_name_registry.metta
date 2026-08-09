; Structural references are explicitly bound.  Looking up a descriptor never
; creates a space, so the public-space vivification policy remains separate.

!(bind! &@(doc (package alpha)) (new-space))
!(bind! &@(doc (package beta)) (new-space))

!(add-atom &@(doc (package alpha)) (entry alpha-text))
!(add-atom &@(doc (package beta)) (entry beta-text))

!(assertEqual
    (reify
      (match &@(doc (package alpha))
             (entry $@(text)) $@(text)))
    (alpha-text))
!(assertEqual
    (reify
      (match &@(doc (package beta))
             (entry $@(text)) $@(text)))
    (beta-text))

; Repeated construction of an equal closed descriptor rendezvous on one entry.
!(assertEqual (size &@(doc (package alpha))) 1)
!(assertEqual (size &@(doc (package beta))) 1)

; Distinct structural descriptors do not alias.
!(assertEqual
    (reify
      (match &@(doc (package alpha))
             (entry beta-text) found))
    ())
!(assertEqual
    (reify
      (match &@(doc (package beta))
             (entry alpha-text) found))
    ())

!(println! (PrimeUniversalNameRegistrySummary 6 6 0))
