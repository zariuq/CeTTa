; Reproducible qualification prelude for official ECAN programs that call
; Python's random module.  The ECAN source remains unmodified; this file only
; fixes the external effect's initial state so default/reference transcripts
; can be compared exactly.

!(py-call (random.seed 20260901))
