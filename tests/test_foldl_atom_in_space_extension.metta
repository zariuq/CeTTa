; CeTTa extension: foldl-atom-in-space is the cleaner explicit-space public
; API, so it should return the same final value as the public foldl wrapper
; rather than the raw HE helper instruction.

!(assertEqual
    (foldl-atom-in-space () 1 $a $b (eval (+ $a $b)) (context-space))
    (foldl-atom () 1 $a $b (eval (+ $a $b))))
!(assertEqual
    (foldl-atom-in-space (1 2 3) 0 $a $b (eval (+ $a $b)) (context-space))
    (foldl-atom (1 2 3) 0 $a $b (eval (+ $a $b))))
