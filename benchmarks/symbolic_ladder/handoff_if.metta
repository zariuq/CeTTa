; A non-boolean condition is the else branch. The later equation still answers.
; Duplicate `no` is the False fact and then `other`.

(= (bit) True)
(= (bit) False)
(= (bit) other)
(= (keep) (let $v (bit) (if $v yes no)))
(= (keep) later)
!(keep)
