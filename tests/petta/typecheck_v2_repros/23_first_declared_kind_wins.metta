; A type name has one source-ordered meaning.  Later conflicting declaration
; kinds are ignored rather than changing the meaning already used by clauses.

(: Stable (Alias Number))
(: Stable (Newtype String))

(: use-stable (-> Stable Number))
(= (use-stable $value) $value)
