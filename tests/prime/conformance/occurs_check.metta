; A finite proof term cannot acquire an infinite type through a cyclic
; substitution.  This rule set used to fabricate an inhabitant of any target
; by solving a type variable as its own negation.

(: Target Type)
(: Good Type)
(: good Good)
(: neg (-> Type Type))
(: impl (-> Type Type Type))

(: pm2.24ii (-> $ph (neg $ph) $ps))
(chaining-rule pm2.24ii)
(: pm2.18i (-> (impl (neg $ph) $ph) $ph))
(chaining-rule pm2.18i)
(: notnotr (impl (neg (neg $ph)) $ph))
(type-scheme notnotr)
(: notnot (impl $ph (neg (neg $ph))))
(type-scheme notnot)

(= (positive-verdict (he-accept $answer)) AcyclicSearchAccepted)
(= (positive-verdict (he-reject $reason)) AcyclicSearchRejected)
(= (cyclic-search-verdict (he-reject (no-inhabitant-at-depth)))
   CyclicSearchRejected)
(= (cyclic-search-verdict (he-accept $answer)) CyclicSearchUnsound)
(= (cyclic-replay-verdict (he-reject $reason)) CyclicReplayRejected)
(= (cyclic-replay-verdict (he-accept $answer)) CyclicReplayUnsound)

; Positive control: ordinary acyclic inference remains accepted.
!(positive-verdict
   (search-first-inhabitant
      &self Good 4 100000
      (search-policy atp-guided-inhabitation)))

; Negative controls: neither search nor direct replay may accept the cyclic
; pseudo-proof as an inhabitant of an unrelated target.
!(cyclic-search-verdict
   (search-first-inhabitant
      &self Target 4 100000
      (search-policy atp-guided-inhabitation)))
!(cyclic-replay-verdict
   (check-type
      &self
      (pm2.24ii (pm2.18i notnot) (pm2.18i notnotr))
      Target
      100000))
