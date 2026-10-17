!(import! &self system)
; A committed write occurs once before choice, not again for every answer.
!(let $effect (system:write stdout "once\n") (superpose (a a)))
