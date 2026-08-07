; Negative mirror: the first Alias String declaration remains authoritative;
; a later Newtype Number declaration cannot launder String into Number.

(: Stable (Alias String))
(: Stable (Newtype Number))

(: use-stable (-> Stable Number))
(= (use-stable $value) $value)
