; A learned predicate used by an ongoing query workflow.
;
; The chain learner of `prime_native_mil_grandparent.metta` learns
; "mother, then father" from one positive and one negative example.  The
; learned program is then queried the way an agent uses it: through an
; interpreted query and through the represented native route, with a cheap
; guard moved ahead of an expensive valuation, over a stream of batches, across
; revisions of the world, and under several observers.  Each step has a
; positive control and a negative one.

!(import! &self ../../lib/ilp/prime_native_mil.metta)

(: sort-code (u 0))
(: person-sort sort-code)
(: number-sort sort-code)
(: primitive-vocabulary
   (-> sort-code (-> sort-code (u 1))))

(: mother-symbol
   (primitive-vocabulary person-sort person-sort))
(: father-symbol
   (primitive-vocabulary person-sort person-sort))
(: successor-symbol
   (primitive-vocabulary number-sort number-sort))

(: mother (-> u0 (-> u0 u1)))
(: father (-> u0 (-> u0 u1)))
(: successor (-> u0 (-> u0 u1)))

(: alice u0)
(: bob u0)
(: carol u0)
(: eve u0)
(: zero u0)
(: one u0)
(: mother-alice-bob (mother alice bob))
(: mother-alice-eve (mother alice eve))
(: father-bob-carol (father bob carol))
(: father-eve-carol (father eve carol))
(: successor-zero-one (successor zero one))

(= (hyp:carrier person-sort) u0)
(= (hyp:carrier number-sort) u0)
(= (hyp:meaning person-sort person-sort mother-symbol) mother)
(= (hyp:meaning person-sort person-sort father-symbol) father)
(= (hyp:meaning number-sort number-sort successor-symbol) successor)

(= (mother alice) (rel:edge bob mother-alice-bob))
(= (mother alice) (rel:edge eve mother-alice-eve))
(= (father bob) (rel:edge carol father-bob-carol))
(= (father eve) (rel:edge carol father-eve-carol))
(= (successor zero) (rel:edge one successor-zero-one))

(= (client:examples)
   (hyp:examples
     ((hyp:example alice carol))
     ((hyp:example alice bob))))

(= (client:program)
   (hyp:learn-chain
     &self sort-code primitive-vocabulary
     person-sort person-sort (client:examples)))

; The learned predicate, its examples and its support.  The program is
; consistent with both examples, and each answer keeps the middle person and
; the two fact proofs it rests on.
!(hyp:consistent (client:program) (client:examples))
!(hyp:outputs (client:program) alice)

; The interpreted query and the represented native route, on the same
; snapshot, give the same ordered occurrences with the same proofs.
(= (client:interpreted $person)
   (let (rel:edge $middle $first) (mother $person)
     (let (rel:edge $target $second) (father $middle)
       (hyp:edge $target
         (hyp:chain-proof $middle
           (hyp:primitive-proof mother-symbol $first)
           (hyp:primitive-proof father-symbol $second))))))

!(== (collapse (client:interpreted alice))
     (hyp:outputs (client:program) alice))

; A cheap guard moved ahead of an expensive valuation.
(= (client:target (hyp:edge $target $proof)) $target)
(= (client:middle (hyp:edge $target (hyp:chain-proof $middle $first $second)))
   $middle)
(= (client:via-bob $edge) (== (client:middle $edge) bob))
(= (client:reaches-carol $edge) (== (client:target $edge) carol))
(= (client:score $edge) (scored (client:middle $edge) (client:target $edge)))

; Eager: value every occurrence, then keep the admitted ones.  The discarded
; valuations stay visible, so the work is observable.
(= (client:eager $guard $edges)
   (collapse
     (let $edge (superpose $edges)
       (let $value (client:score $edge)
         (if ($guard $edge) (kept $value) (discarded $value))))))

; Hoisted: guard first, and value only the admitted occurrences.
(= (client:hoisted $guard $edges)
   (collapse
     (let $edge (superpose $edges)
       (if ($guard $edge) (kept (client:score $edge)) (empty)))))

(= (client:kept $rows)
   (collapse
     (let (kept $value) (superpose $rows) $value)))

(= (client:alice-edges) (hyp:outputs (client:program) alice))

; The guard admits one of the two occurrences.  Same answers; the eager route
; valued two occurrences and the hoisted route one.
!(client:eager client:via-bob (client:alice-edges))
!(client:hoisted client:via-bob (client:alice-edges))
!(== (client:kept (client:eager client:via-bob (client:alice-edges)))
     (client:kept (client:hoisted client:via-bob (client:alice-edges))))
!(let* (($eager (client:eager client:via-bob (client:alice-edges)))
        ($hoisted (client:hoisted client:via-bob (client:alice-edges))))
   (valuations (size-atom $eager) (size-atom $hoisted)))

; A guard admitting both occurrences keeps the duplicate answer, in order.
!(client:kept (client:hoisted client:reaches-carol (client:alice-edges)))

; Every finite requested prefix of a stream of batches agrees.
(= (client:batch alice) (client:alice-edges))
(= (client:batch bob) ())
(= (client:prefix $route $guard ()) ())
(= (client:prefix $route $guard $inputs)
   (let ($input $rest) (decons-atom $inputs)
     (let $head (client:kept ($route $guard (client:batch $input)))
       (let $tail (client:prefix $route $guard $rest)
         (cons-atom $head $tail)))))

!(client:prefix client:hoisted client:via-bob (alice bob alice))
!(== (client:prefix client:eager client:via-bob (alice bob alice))
     (client:prefix client:hoisted client:via-bob (alice bob alice)))

; Observers.  Values alone keep the count only if duplicates are kept, and
; they never say which middle supported an answer.
(= (client:values $edges)
   (collapse (let $edge (superpose $edges) (client:target $edge))))
(= (client:middles $edges)
   (collapse (let $edge (superpose $edges) (client:middle $edge))))

!(client:values (client:alice-edges))
!(let $values (client:values (client:alice-edges))
   (let $distinct (unique-atom $values)
     (counts (size-atom $values) (size-atom $distinct))))
!(client:middles (client:alice-edges))

; An effectful valuation.  Moving the guard ahead of it changes the effect:
; the hoisted route logs one valuation and the eager route two.
!(bind! &hoisted-log (new-space))
!(bind! &eager-log (new-space))
(= (client:logged-score $log $edge)
   (let () (add-atom $log (valued (client:middle $edge)))
     (client:score $edge)))
!(let $edges (client:alice-edges)
   (collapse
     (let $edge (superpose $edges)
       (if (client:via-bob $edge)
           (client:logged-score &hoisted-log $edge)
           (empty)))))
!(let $edges (client:alice-edges)
   (collapse
     (let $edge (superpose $edges)
       (let $value (client:logged-score &eager-log $edge)
         (if (client:via-bob $edge) $value (empty))))))
!(collapse (match &hoisted-log (valued $middle) $middle))
!(collapse (match &eager-log (valued $middle) $middle))

; Revisions.  Keep the answer computed before any revision.  The answer is
; computed first and then stored, so the cache holds a value, not a query.
!(bind! &cache (new-space))
!(let $answer (client:kept (client:hoisted client:reaches-carol (client:alice-edges)))
   (add-atom &cache (cached alice $answer)))

; An irrelevant revision: the learned chain reads no successor fact.  The
; cached answer is still the answer.
!(remove-atom &self (= (successor zero) (rel:edge one successor-zero-one)))
!(let (cached alice $old) (match &cache (cached alice $answer) (cached alice $answer))
   (== $old
       (client:kept (client:hoisted client:reaches-carol (client:alice-edges)))))

; A relevant revision: one route to carol is gone.  The cached answer is now
; stale, and the recomputed one has a single occurrence.
!(remove-atom &self (= (father eve) (rel:edge carol father-eve-carol)))
!(client:kept (client:hoisted client:reaches-carol (client:alice-edges)))
!(let (cached alice $old) (match &cache (cached alice $answer) (cached alice $answer))
   (== $old
       (client:kept (client:hoisted client:reaches-carol (client:alice-edges)))))

; Same values, different explanation: with a second route through dan, the
; values are again two carols, but the middles differ.  An observer of
; explanations cannot be served from values alone.
(: dan u0)
(: mother-alice-dan (mother alice dan))
(: father-dan-carol (father dan carol))
!(add-atom &self (= (mother alice) (rel:edge dan mother-alice-dan)))
!(add-atom &self (= (father dan) (rel:edge carol father-dan-carol)))
!(client:values (client:alice-edges))
!(client:middles (client:alice-edges))
