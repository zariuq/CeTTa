; A source-authored scalar guard may refine overlapping clauses before the
; deterministic prepared body runs.  The exact clause owns zero; the guarded
; recursive clause owns every positive integer.
(= (guarded-count 0) done)
(= (guarded-count $n)
   (if (> $n 0)
       (guarded-count (- $n 1))
       (empty)))
!(guarded-count 1000)

; The symmetric orientation is equally admissible: a false condition selects
; the nonzero branch when the authored true branch is choice zero.
(= (guarded-nonpositive $n)
   (if (> $n 0) (empty) nonpositive))
!(guarded-nonpositive 0)
; A refuted sole clause has no answer.  It must not turn choice zero into data.
!(guarded-nonpositive 1)

; Two surviving guarded clauses remain nondeterministic.  Whole-program
; fallback preserves equal occurrence multiplicity rather than committing one.
(= (guarded-duplicate $n) (if (> $n 0) same (empty)))
(= (guarded-duplicate $n) (if (> $n 0) same (empty)))
!(guarded-duplicate 1)

; A nested pattern is outside the scalar-guard certificate.  The canonical
; matcher remains authoritative and still returns the correct binding.
(= (guarded-nested (Box $n))
   (if (> $n 0) $n (empty)))
!(guarded-nested (Box 2))

; Computed guard operands are likewise outside the bounded slot observer.
; Canonical evaluation remains exact.
(= (guarded-computed $n)
   (if (> (+ $n 0) 0) computed (empty)))
!(guarded-computed 1)
