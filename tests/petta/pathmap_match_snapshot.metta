; A binding snapshot owns the same alternatives as an occurrence snapshot.
; Returning one ground answer must not retire its duplicate alternatives.
!(bind! &snapshot (new-space))
!(add-atom &snapshot (row a 1))
!(add-atom &snapshot (row a 1))
!(match &snapshot (row a 1) hit)
!(collapse (match &snapshot (row a 1) hit))
!(size-atom (collapse (match &snapshot (row a 1) hit)))
!(collapse (once (match &snapshot (row a 1) hit)))
!(collapse (match &snapshot (row missing 1) hit))

; A template failure cannot erase the remaining duplicate alternatives.
!(collapse
   (match &snapshot (row a 1)
     (match &snapshot (row missing 1) never)))
!(collapse (match &snapshot (row a $value) $value))

; The logical-update snapshot must exclude rows added by its own template.
!(collapse
   (match &snapshot (row a 1)
     (let $done (add-atom &snapshot (row a 1)) seen)))
!(size-atom (collapse (match &snapshot (row a 1) hit)))

; Removal on the first answer must not invalidate the already-owned rows.
!(collapse
   (match &snapshot (row a 1)
     (let $done (remove-atom &snapshot (row a 1)) removed)))
!(collapse (match &snapshot (row a 1) hit))

; Stored variables must still take their semantic matcher, with each stored
; occurrence retaining an independent opening context.
!(add-atom &snapshot (open $item))
!(add-atom &snapshot (open $item))
!(collapse (match &snapshot (open fixed) opened))
