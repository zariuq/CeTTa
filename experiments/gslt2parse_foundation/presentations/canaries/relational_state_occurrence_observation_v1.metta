; Observable summary of ordered occurrence-stream execution.  Execution runs
; forward from the authored initial state; classification and depth projection
; observe its result without turning an expected verdict into a search plan.

(gslt-presentation-v1 RelationalStateOccurrenceObservationV1
  (signature
    (operator StateOccurrenceSummaryV1 2)
    (operator StateDepthV1 2)
    (operator StateStartOccurrenceStreamV1 5)
    (operator OutcomeClassV1 2)
  )
  (equations)
  (rewrites
    (rule state-depth-v1
      (head
        (StateDepthV1
          (StateV1 ?package ?sequence ?depth ?tables) ?depth))
      (body))

    (rule state-occurrence-summary-v1
      (head
        (StateOccurrenceSummaryV1 ?class ?depth))
      (body
        (StateStartOccurrenceStreamV1
          ?package ?stream ?outcome ?state ?history)
        (OutcomeClassV1 ?outcome ?class)
        (StateDepthV1 ?state ?depth)))
  ))
