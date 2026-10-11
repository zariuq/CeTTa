(set:profile megalodon-hotg)

; Imported by cross_profile_citations.metta: a library that selects
; megalodon-hotg and publishes a logical theorem under it.
!(set:theorem lib-self-imp (all prop (lam a (imp a a))) (pf:all-intro (pf:imp-intro (pf:hyp 0))))
