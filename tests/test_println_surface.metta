; HE-backed grounded op: println! prints the atom and returns unit.
; `let ()` is the HE-compatible sequencing pattern used by stdlib callers.

!(let () (println! "hello") ok)
