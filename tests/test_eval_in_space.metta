; eval with an explicit space argument — the explicit-context spelling of evalc.
!(bind! &ctx (new-space))
!(add-atom &ctx (= (ctx-fn) ctx-result))
!(assertEqual (eval (ctx-fn) &ctx) ctx-result)
!(assertEqual (evalc (ctx-fn) &ctx) ctx-result)
