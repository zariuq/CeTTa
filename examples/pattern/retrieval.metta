!(import! &self pat)
(edge $stored b)
(edge a b)
(edge a b)

; Ordinary matching may instantiate either side; all three occurrences count.
!(collapse (match &self (edge a $y) $y))
; A pattern query cannot fill $stored to make that open row into (edge a b).
!(collapse
   (pat:match% &self (edge a $y) (destination $y)))
; The converse exposes each original stored pattern without a result record.
!(collapse (pat:%match &self (edge a b) $stored (covered $stored)))
; Inspect or reuse a substitution through an explicit query witness.
!(collapse
   (let (pat:hit $rows $w) (pat:query &self match% (edge a $y))
     (pat:apply $w (destination $y))))

; Converse matching retrieves stored patterns covering this fixed query.
; Keep the original row, its instantiated view, and its occurrence identity.
!(collapse
   (let (pat:hit ((pat:row $occurrence $original $view)) $w)
        (pat:query &self %match (edge a b))
     (retrieved $occurrence $original $view (pat:apply $w $original))))
; Variant checking preserves the repeated-variable structure under renaming.
!(collapse
   (let (pat:hit $rows $renaming) (pat:query &self variant (edge $q b))
     (pat:apply $renaming (edge $q b))))
