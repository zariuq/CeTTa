; A non-tail user equation leaves its recursive child suspended in Prime.
; Full observation must repeatedly re-enter the revision-pinned prepared
; program without retaining invocation arguments between calls or growing the
; native C stack.
(= (prime-stack-observe:i2p $n)
   (if (< $n 1)
       Z
       (S (prime-stack-observe:i2p (- $n 1)))))

!(prime-stack-observe:i2p 10000)
