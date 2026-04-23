!(bind! &batched (new-space))
!(add-atoms &batched ((essence dew)))
!(match &batched (essence $y) $y)
!(match &batched $x (quote $x))
