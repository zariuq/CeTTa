; A demanded output meets a builtin's occurrence as it will be reached.  The
; builtin's own output is known while the current program holds no authored
; alternative of it and no goal before the occurrence can change the program:
; the recursive alternative is refuted before its body runs.  An alternative
; installed by a goal before the occurrence leaves the output open, and the
; occurrence runs it where a profile permits one (the extended profile; the
; base profile keeps a builtin's definition its own).
(= (nat) Z)
(= (nat) (let $n (nat) (S $n)))
!(Nat (let Z (nat) yes))
(= (installed) (let $u (add-atom &self (= (once $x) authored)) (once Z)))
!(Installed (collapse (let authored (installed) yes)))
!(After (collapse (once Z)))
