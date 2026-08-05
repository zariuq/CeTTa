; A generated pure call returns a deeply nested symbolic value.  Prime may
; keep constructor fields suspended internally, but full observation must
; consume the structure without borrowing depth from the native C stack.
(= (prime-observe:build $n $acc)
   (if (< $n 1)
       $acc
       (prime-observe:build (- $n 1) (S $acc))))

!(prime-observe:build 10000 Z)
