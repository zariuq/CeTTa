; Correlated absence and query boundaries from SourceAbsenceControls.
!(import! &self interaction)
!(bind! &messages (new-space))
!(add-atom &messages (msg a))
!(add-atom &messages (ack b))

; Both patterns have matches, but no value joins them.
!(collapse (transform &messages
   (absent (, (read (msg $x)) (read (ack $x)))) no-joined-row))
!(collapse (transform &messages (absent (read (msg $x))) no-message))
!(collapse (transform &messages (absent (,)) no-empty-conjunction))

; One matching acknowledgement makes the correlated query nonempty.
!(add-atom &messages (ack a))
!(collapse (transform &messages
   (absent (, (read (msg $x)) (read (ack $x)))) no-joined-row))

; Compare aggregate-row semantics with the native absence walk.
!(bind! &empty (new-space))
!(collapse (transform &empty
   (absent (every (read (msg $x)))) no-every-row))

; A computed source is a resumable request in the native walk.
!(collapse (transform &empty
   (absent (let $x 7)) no-computed-row))

; Conjunction wrapping makes both forms enter the inner-source walk.
!(collapse (transform &empty
   (absent (, (every (read (msg $x))))) no-wrapped-every-row))
!(collapse (transform &empty
   (absent (, (let $x 7))) no-wrapped-computed-row))
