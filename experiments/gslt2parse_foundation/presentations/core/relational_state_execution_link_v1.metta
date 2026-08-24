; Connect a compiled operation program to the generic state-transition
; relation.  This presentation adds no action meaning: it only selects the
; source-bound program and threads its initial state into ordered execution.

(gslt-presentation-v1 RelationalStateExecutionLinkV1
  (signature
    (operator StateExecuteOperationV1 7)
    (operator StateStartOperationV1 6)
  )
  (equations)
  (rewrites
    (rule state-execute-operation-v1
      (head
        (StateExecuteOperationV1
          ?package ?operation ?occurrence ?before
          ?outcome ?after ?history))
      (body
        (StateOperationProgramV1 ?package ?operation ?actions)
        (RunActionsV1
          ?package ?occurrence ?before ?actions
          ?outcome ?after ?history)))
    (rule state-start-operation-v1
      (head
        (StateStartOperationV1
          ?package ?operation ?occurrence
          ?outcome ?after ?history))
      (body
        (StateInitialStateV1 ?package ?initial)
        (StateExecuteOperationV1
          ?package ?operation ?occurrence ?initial
          ?outcome ?after ?history)))
  ))
