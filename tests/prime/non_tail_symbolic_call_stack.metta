; A non-tail user equation constructs one symbolic layer after its recursive
; result returns.  The call/return continuation must live in generated machine
; frames rather than on the native C stack.
(= (prime-stack:i2p $n)
   (if (< $n 1)
       Z
       (S (prime-stack:i2p (- $n 1)))))

; Consume the symbolic result through a second machine loop.  This checks the
; exact depth without making the shell materialize a 40-KiB expected string;
; the atom printer has its own 100,000-deep structural gate.
(= (prime-stack:depth Z $n) $n)
(= (prime-stack:depth (S $rest) $n)
   (prime-stack:depth $rest (+ $n 1)))

!(prime-stack:depth (prime-stack:i2p 10000) 0)
