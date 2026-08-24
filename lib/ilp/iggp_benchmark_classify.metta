; Classify a complete IGGP proof-occurrence bag without collapsing its paths.
; Each occurrence is checked against the exact episode-indexed target.  An
; empty bag means only that this program produced no derivation; it is not a
; Prime Refuted verdict.

(= (iggp:proof-checks (quote $goal) $occurrences)
  (collapse
    (let (occurrence $proof-data)
         (superpose $occurrences)
      (type:check (unquote $proof-data) $goal))))

(= (iggp:classify-occurrences $name (quote $goal) $occurrences)
  (let $count (- (size-atom $occurrences) 1)
    (let $checks
         (iggp:proof-checks (quote $goal) $occurrences)
      (if (== $count 0)
          (iggp:case $name not-derived $count $checks)
          (iggp:case $name derived $count $checks)))))
