; COUNT over COLLAPSE is a fold over the exact answer bag.  Duplicate
; occurrences count independently, the PeTTa unit tuple remains an ordinary
; result, and a genuinely failed branch contributes no result.
(counted a)
(counted a)
(counted b)
(counted-dynamic-left count-dynamic-marker)
(counted-dynamic-left count-dynamic-marker)
(counted-dynamic-right count-dynamic-marker)
(counted-dynamic-right other-marker)
(counted-nested (tag 1) alpha)
(counted-nested (tag 1) beta)
(counted-nested (tag 2) gamma)
(counted-empty-data Empty)
(counted-mixed-error (Error source hidden))
(counted-mixed-error ordinary)
(counted-all-error (Error source first))
(counted-all-error (Error source second))
(counted-drop a)
(counted-join-seed alpha)
(counted-join-seed beta)
(counted-join-guard alpha)
(counted-join-guard alpha)
(counted-join-tail left)
(counted-join-tail middle)
(counted-join-tail right)

(= (drop-counted-template $value) (empty))

(: counted-control-choice (-> Number))
(= (counted-control-choice) 7)
(= (counted-control-choice) (+ 1 not-a-number))

(= (counted-items)
   (collapse (match &self (counted $x) $x)))

!(length (collapse (match &self (counted $x) $x)))
!(length (collapse (match &self (counted $x) ())))
!(length (collapse (superpose (a a ()))))
!(length (collapse (empty)))

; An ANSWER occurrence is distinct from computational zero.  A value
; occurrence carrying the symbol Empty remains visible, while evaluating
; `(empty)` contributes no occurrence.
!(collapse (match &self (counted-empty-data $value) $value))
!(length (collapse (match &self (counted-empty-data $value) $value)))
!(collapse (superpose (Empty)))
!(collapse (superpose ((empty))))

; Payload spelling does not classify an answer occurrence.  Error-headed
; values produced by a translation-time VALUE occurrence remain ordinary data,
; while a computed zero-result template must decline the single-answer
; contract.
!(collapse
   (match &self (counted-mixed-error $value) $value))
!(length
   (collapse
     (match &self (counted-mixed-error $value) $value)))
!(length
   (collapse
     (match &self (counted-all-error $value) $value)))
!(length
   (collapse
     (match &self (counted-drop $value)
            (drop-counted-template $value))))

; An error raised by one alternative leaves the collection, as an exception
; leaves findall, whether the collection is kept or only counted; catch
; returns it.
!(catch (collapse (counted-control-choice)))
!(catch (length (collapse (counted-control-choice))))

; A ground conjunction leg contributes its exact occurrence multiplicity to
; the surrounding count.  Alpha's duplicated guard and the three tail rows
; therefore denote six answers; beta is pruned by the absent ground guard.
!(length
   (collapse
     (match &self
            (, (counted-join-seed $key)
               (counted-join-guard $key)
               (counted-join-tail $tail))
            ())))

; A computed template is not a one-datum producer.  It retains the ordinary
; conjunction path and its zero outcomes rather than inheriting the count
; weight merely from the matching rows.
!(length
   (collapse
     (match &self
            (, (counted-join-seed $key)
               (counted-join-guard $key)
               (counted-join-tail $tail))
            (drop-counted-template $tail))))

; A collection returned through an ordinary relation may remain a count while
; every observation is length.  Repeated observations reuse that exact count.
!(let* (($items (counted-items)))
       (pair (length $items) (length $items)))

; Any materializing observation rejects count-only fusion.  The original bag,
; including duplicate occurrences and order, remains visible.
!(let* (($items (counted-items)))
       (pair (length $items) $items))

; A variable relation head is data in a match template, not a call to
; evaluate.  COUNT may therefore stay below reconstruction while preserving
; duplicates and all matching heads.
!(length
   (collapse
     (match &self
            ($relation count-dynamic-marker)
            ($relation count-dynamic-marker))))

; Stable compound keys and absent keys exercise the same bag fold without
; making the physical equality representation part of the language contract.
!(length
   (collapse
     (match &self (counted-nested (tag 1) $value) $value)))
!(length
   (collapse
     (match &self (counted-nested (tag 9) $value) $value)))

; The same shape outside COUNT remains fully materialized.
!(collapse
   (match &self
          ($relation count-dynamic-marker)
          ($relation count-dynamic-marker)))
