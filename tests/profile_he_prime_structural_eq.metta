; he-prime intentionally exposes total structural equality for == after typed
; equality cannot apply.

!(assertEqual (== 1 "S") False)
!(assertEqual (== (42 (1 2 3)) (42 1 2 3)) False)
!(assertEqual (== (foo (1 2 3)) (foo 1 2 3)) False)
