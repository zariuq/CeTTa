; Cardinality is an occurrence-bag fold.  The same private-use judgment must
; commute with direct let, sequential let*, equation unfolding, duplicates,
; and empty sources in every dialect that admits size-atom.
(cardinality-source alpha left)
(cardinality-source alpha left)
(cardinality-source alpha right)
(cardinality-source beta only)

(= (cardinality-items $key)
   (collapse
     (match &self
            (cardinality-source $key $value)
            $value)))

(= (cardinality-direct $key)
   (let $items (cardinality-items $key)
     (counts (size-atom $items) (size-atom $items))))

(= (cardinality-sequential $key)
   (let* (($forwarded $key)
          ($items (cardinality-items $forwarded)))
     (counts (size-atom $items) (size-atom $items))))

; A non-cardinality use keeps the ordinary materializing path and its
; duplicate occurrences.
(= (cardinality-materialized $key)
   (let $items (cardinality-items $key)
     (pair (size-atom $items) (superpose $items))))

; Multiple source equations produce two collection values.  They must remain
; two counted outcomes rather than being contracted into one total.
(= (cardinality-choice)
   (collapse
     (match &self (cardinality-source alpha $value) $value)))
(= (cardinality-choice)
   (collapse
     (match &self (cardinality-source beta $value) $value)))
(= (cardinality-choice-counts)
   (let $items (cardinality-choice) (size-atom $items)))

!(cardinality-direct alpha)
!(cardinality-sequential alpha)
!(cardinality-sequential missing)
!(cardinality-materialized alpha)
!(cardinality-choice-counts)
