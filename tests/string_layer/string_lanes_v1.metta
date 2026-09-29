; A string holding NUL, in every lane.  It is built from its codepoints,
; since the lanes' readers spell NUL differently: the HE readers read \x00,
; and the PeTTa and Prime readers the byte itself.  Each check prints pass or
; fail; the gate counts the passes, so a check that yields nothing fails too.

!(import! &self str)

(= (lanes:nul-b) (str:from-codepoints (97 0 98)))
(= (lanes:nul-c) (str:from-codepoints (97 0 99)))
(= (lanes:is $truth) (if $truth pass fail))

; It keeps its three bytes and compares by all of them.
!(lanes:is (== (str:byte-length (lanes:nul-b)) 3))
!(lanes:is (== (lanes:nul-b) (lanes:nul-b)))
!(lanes:is (== (== (lanes:nul-b) (lanes:nul-c)) False))
!(lanes:is (== (str:compare (lanes:nul-b) (lanes:nul-c)) -1))

; It prints and reads back exactly.
!(let $b (lanes:nul-b) (lanes:is (== (parse (repr $b)) $b)))
!(let $b (lanes:nul-b) (let $c (lanes:nul-c) (lanes:is (== (== (parse (repr $b)) $c) False))))

; Stored and matched, it is found by all its bytes and only by them.
!(let $b (lanes:nul-b) (add-atom &self (lanes:entry $b left)))
!(let $c (lanes:nul-c) (add-atom &self (lanes:entry $c right)))
!(let $b (lanes:nul-b) (lanes:is (== (match &self (lanes:entry $b $side) $side) left)))

; Copied into a concatenation, it keeps every byte.
!(let $b (lanes:nul-b) (lanes:is (== (str:byte-length (str:concat $b $b)) 6)))
