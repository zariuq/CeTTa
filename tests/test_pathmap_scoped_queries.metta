; Scope changes preserve query cells; storage and access paths preserve rows.
(= (scoped-prefix $space $key)
   (match $space (edge $key $value) $value))
(= (scoped-suffix $space $key)
   (match $space (edge $value $key) $value))
(= (scoped-diagonal $space)
   (match $space (edge $same $same) $same))

!(bind! &scoped-rows (new-space pathmap))
!(add-atom &scoped-rows (edge a b))
!(add-atom &scoped-rows (edge b c))
!(add-atom &scoped-rows (edge b c))
!(let $rows (collapse (scoped-prefix &scoped-rows a)) (sort-atom $rows))
!(let $rows (collapse (scoped-suffix &scoped-rows c)) (sort-atom $rows))
!(collapse (scoped-prefix &scoped-rows missing))
!(collapse (scoped-diagonal &scoped-rows))

; A stored variable restores the authoritative bidirectional matcher.
!(add-atom &scoped-rows (edge $stored c))
!(let $rows (collapse (scoped-suffix &scoped-rows c)) (size-atom $rows))
!(let $rows (collapse (scoped-prefix &scoped-rows a)) (sort-atom $rows))
!(let $rows (collapse (scoped-diagonal &scoped-rows)) (size-atom $rows))
