!(import! &self (library lib_memo))

!(memoize emit)
(= (emit $x) (add-atom &self (seen $x)))

; Base PeTTa follows lib_memo's static-world cache: the second call replays
; the first result and therefore does not repeat its write.  The extended
; profile instead declines effectful memoization and performs both writes.
!(emit 1)
!(emit 1)
!(collapse (match &self (seen 1) hit))
!(get-memoize-stats)
