(gslt-presentation-v1 SyntaxLookaheadCoreV1
  (signature
    (operator peek 1)
  )
  (equations)
  (rewrites
    (rule parse-peek (head (parse (peek ?grammar) ?input ?value ?input)) (body (parse ?grammar ?input ?value ?after-lookahead)))
  ))
