; Test: discrimination trie correctness with many equations per head symbol
; All equations share head "f" → they go into the same EqIndex bucket

; Simple exact matches
(= (f a) result-a)
(= (f b) result-b)
(= (f c) result-c)
(= (f d) result-d)
(= (f e) result-e)
(= (f x) result-x)

; Test exact lookup
! (f a)
! (f c)
! (f x)

; Nested expression LHS
(= (g (h a)) nested-a)
(= (g (h b)) nested-b)
(= (g (h c)) nested-c)
(= (g (h d)) nested-d)
(= (g (h e)) nested-e)
(= (g (h f)) nested-f)
(= (g (h (k z))) deep-nested)

; Test nested lookup
! (g (h a))
! (g (h f))
! (g (h (k z)))

; Variable LHS — matches any query
(= (p $x) (got $x))
(= (p specific) found-specific)
(= (p a) found-a)
(= (p b) found-b)
(= (p c) found-c)
(= (p d) found-d)

; Variable query — matches any LHS
! (p specific)
! (p a)

; Many equations to force trie usage (bucket > 4)
(= (m 1) one)
(= (m 2) two)
(= (m 3) three)
(= (m 4) four)
(= (m 5) five)
(= (m 6) six)

! (m 1)
! (m 4)
! (m 6)
