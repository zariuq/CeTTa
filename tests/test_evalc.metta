; evalc — evaluate in a specific context space
; Ref: metta.md line 173-174
!(bind! &ctx (new-space))
!(add-atom &ctx (= (ctx-fn) ctx-result))
!(assertEqual (evalc (ctx-fn) &ctx) ctx-result)
