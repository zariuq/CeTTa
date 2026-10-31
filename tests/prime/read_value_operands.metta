; A value a step reads from a space is the same operand to a native
; operation as the value written in place: no internal carrier shows.
!(import! &self interaction)
!(bind! &operands (new-space))
!(add-atom &operands (words ("b" "c" "a")))
!(add-atom &operands (members (3 1 2 1)))

!(format-args "{} {}" ("b" "c"))
!(transform &operands (read (words $w)) (formatted (format-args "{} {}" $w)))
!(transform &operands (read (members $m)) (formatted (format-args "{}-{}" $m)))
!(sort-strings ("b" "c" "a"))
!(transform &operands (read (words $w)) (sorted (sort-strings $w)))
!(transform &operands (read (words $w)) (printed (print-alternatives! words $w)))
!(transform &operands (read (members $m)) (sets (union-atom $m (9)) (subtraction-atom $m (1)) (intersection-atom $m (1 3))))

; Operands that are not expressions of the right kind are still refused.
!(format-args "{}" 5)
!(transform &operands (read (members $m)) (sorted (sort-strings $m)))
