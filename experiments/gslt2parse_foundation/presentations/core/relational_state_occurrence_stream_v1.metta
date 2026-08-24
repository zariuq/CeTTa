; Ordered execution of parser occurrences through an authored state program.
; The source compiler supplies only the stream topology and operation attached
; to each occurrence.  Operation meaning remains in StateOperationProgramV1
; and StateExecuteOperationV1.

(gslt-presentation-v1 RelationalStateOccurrenceStreamV1
  (signature
    (operator StateOccurrenceProgramV1 2)
    (operator StateOccurrenceEmptyV1 1)
    (operator StateOccurrenceFirstV1 2)
    (operator StateOccurrenceNextV1 3)
    (operator StateOccurrenceLastV1 2)
    (operator StateOccurrenceShapeV1 2)
    (operator StateOccurrenceNonemptyShapeV1 1)
    (operator StateOccurrenceTailV1 3)
    (operator StateOccurrenceNextTailV1 1)
    (operator StateOccurrenceOperationV1 2)
    (operator StateInitialStateV1 2)
    (operator StateExecuteOperationV1 7)
    (operator RunOccurrenceStreamV1 7)
    (operator RunOccurrenceStreamByTailV1 8)
    (operator RunOccurrenceStreamAfterStepV1 11)
    (operator StateStartOccurrenceStreamV1 5)
    (operator StateStartOccurrenceStreamByShapeV1 7)
    (operator HistoryAppendV1 3)
    (operator OutcomeClassV1 2)
    (operator OutcomeIsRefusalV1 1)
  )
  (equations)
  (rewrites
    (rule run-occurrence-stream-v1
      (head
        (RunOccurrenceStreamV1
          ?package ?stream ?occurrence ?before
          ?outcome ?after ?history))
      (body
        (StateOccurrenceTailV1 ?stream ?occurrence ?tail)
        (RunOccurrenceStreamByTailV1
          ?tail ?package ?stream ?occurrence ?before
          ?outcome ?after ?history)))

    (rule run-occurrence-stream-by-tail-last-v1
      (head
        (RunOccurrenceStreamByTailV1
          StateOccurrenceLastTailV1
          ?package ?stream ?occurrence ?before
          ?outcome ?after ?history))
      (body
        (StateOccurrenceOperationV1 ?occurrence ?operation)
        (StateExecuteOperationV1
          ?package ?operation ?occurrence ?before
          ?outcome ?after ?history)))

    (rule run-occurrence-stream-by-tail-next-v1
      (head
        (RunOccurrenceStreamByTailV1
          (StateOccurrenceNextTailV1 ?next)
          ?package ?stream ?occurrence ?before
          ?outcome ?after ?history))
      (body
        (StateOccurrenceOperationV1 ?occurrence ?operation)
        (StateExecuteOperationV1
          ?package ?operation ?occurrence ?before
          ?stepOutcome ?middle ?stepHistory)
        (OutcomeClassV1 ?stepOutcome ?class)
        (RunOccurrenceStreamAfterStepV1
          ?class ?package ?stream ?next ?before ?middle
          ?stepOutcome ?stepHistory ?outcome ?after ?history)))

    (rule run-occurrence-stream-after-step-accepted-v1
      (head
        (RunOccurrenceStreamAfterStepV1
          OutcomeAcceptedV1 ?package ?stream ?next ?before ?middle
          AcceptedV1 ?stepHistory ?outcome ?after ?history))
      (body
        (RunOccurrenceStreamV1
          ?package ?stream ?next ?middle
          ?outcome ?after ?restHistory)
        (HistoryAppendV1 ?stepHistory ?restHistory ?history)))

    (rule run-occurrence-stream-after-step-refused-v1
      (head
        (RunOccurrenceStreamAfterStepV1
          OutcomeRefusedV1 ?package ?stream ?next ?before ?middle
          ?stepOutcome ?stepHistory ?stepOutcome ?before ?stepHistory))
      (body))

    (rule state-start-occurrence-stream-v1
      (head
        (StateStartOccurrenceStreamV1
          ?package ?stream ?outcome ?after ?history))
      (body
        (StateOccurrenceProgramV1 ?stream ?package)
        (StateInitialStateV1 ?package ?initial)
        (StateOccurrenceShapeV1 ?stream ?shape)
        (StateStartOccurrenceStreamByShapeV1
          ?shape ?package ?stream ?initial ?outcome ?after ?history)))

    (rule state-start-occurrence-stream-by-shape-nonempty-v1
      (head
        (StateStartOccurrenceStreamByShapeV1
          (StateOccurrenceNonemptyShapeV1 ?first)
          ?package ?stream ?initial ?outcome ?after ?history))
      (body
        (RunOccurrenceStreamV1
          ?package ?stream ?first ?initial
          ?outcome ?after ?history)))

    (rule state-start-occurrence-stream-by-shape-empty-v1
      (head
        (StateStartOccurrenceStreamByShapeV1
          StateOccurrenceEmptyShapeV1
          ?package ?stream ?initial AcceptedV1 ?initial HistoryNilV1))
      (body))
  ))
