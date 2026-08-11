!(import_prolog_function
  (superpose (memoize get-memoize-stats)))

!(memoize emit)
(= (emit $x) (add-atom &self (seen $x)))

; An effectful relation is never cached.  Both calls perform their write,
; and the memo statistics remain empty because the table route declined.
!(emit 1)
!(emit 1)
!(collapse (match &self (seen 1) hit))
!(get-memoize-stats)
