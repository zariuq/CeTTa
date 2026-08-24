; Classify a complete RuleMachine proof-occurrence bag without collapsing
; multiplicity.  Every occurrence is checked against the exact indexed goal;
; the returned expression preserves one checking observation per occurrence.

(= (popper:proof-checks (quote $goal) $occurrences)
  (collapse
    (let (occurrence $proof-data)
         (superpose $occurrences)
      (type:check (unquote $proof-data) $goal))))

(= (popper:classify-occurrences $name (quote $goal) $occurrences)
  (let $count (- (size-atom $occurrences) 1)
    (let $checks
         (popper:proof-checks (quote $goal) $occurrences)
      (if (== $count 0)
          (popper:case $name not-derived $count $checks)
          (popper:case $name derived $count $checks)))))
