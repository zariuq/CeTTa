; Direct minimal witness for renamed-wrapper relation export.

(= (f $X $Y)
   (append ($X) $Y))

(= (same $Head $Tail)
   (append ($Head) $Tail))

!(test (let (same $Head $Tail) (1 2 3 4) ($Head $Tail))
       (1 (2 3 4)))

!(test (let (f $Head $Tail) (1 2 3 4) ($Head $Tail))
       (1 (2 3 4)))
