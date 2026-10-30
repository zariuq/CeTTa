; Computing a space name preserves the context lookup of the direct name.
; This also holds when self is selected by a context profile.
!(bind! &computed-other (new-space))
!(add-atom &computed-other (computed-marker other))
!(add-atom &self (computed-marker root))
!(evalc (evalc (match &here (computed-marker $x) $x)
              (if True &here &self)) &computed-other)
!(run (ctx:capture &computed-other
   (evalc (match &here (computed-marker $x) $x) (if True &home &self))))
!(assertEqual
   (run (ctx:capture &computed-other
     (evalc (match &here (computed-marker $x) $x) (if True &self &computed-other))))
   (run (ctx:capture &computed-other
     (evalc (match &here (computed-marker $x) $x) &self))))
