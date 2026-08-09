; Experimental semantic work/continuation layer for MeTTa Zero.  It exposes
; resumable language configurations without exposing a C stack, Horn goal
; stack, arena, or engine queue.  One `zero-step` request performs exactly one
; completed kernel step.  A successful rewrite yields pending work; exact
; emptiness either completes or applies the next authored continuation.
(gslt-presentation-v1 MettaZeroSemanticRunnerV1
  (signature
    (operator gslt-produced 2)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1)
    (operator qmatch-env-cons 3)
    (operator qmatch-substitute 3)
    (operator zero-classify 3)
    (operator zero-continue 3)
    (operator zero-evaluate-request 2)
    (operator zero-observe 4)
    (operator zero-produce 4)
    (operator zero-produced-cons 2)
    (operator zero-produced-member 2)
    (operator zero-step-completed-evidence 2)
    (operator zero-step-pending-evidence 2)
    (operator zero-step-request 3))
  (equations)
  (rewrites
    ; Surface configuration:
    ;   (zero-step (zero-pending subject continuation))
    ; `zero-halt` completes with the first quiescent value.  A continuation
    ; `(zero-then template next)` substitutes that value for the template's
    ; first object variable and schedules the resulting subject.
    (rule zero-classify-semantic-step
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "zero-step"))
            (q-cons
              (q-app
                (q-sym (q-str "zero-pending"))
                (q-cons ?subject (q-cons ?continuation q-nil)))
              q-nil))
          (zero-step-request
            ?request-occurrence ?subject ?continuation)))
      (body))

    ; The runner delegates its transition to the ordinary query-derived
    ; evaluator.  It does not own another equation table or matching path.
    (rule zero-produce-semantic-step
      (head
        (zero-produce
          ?program
          (zero-step-request
            ?request-occurrence ?subject ?continuation)
          ?evidence ?result))
      (body
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          ?evidence ?result)))

    ; Every successful kernel result becomes a distinct pending work
    ; occurrence.  Multiplicity is preserved and no scheduler order is fixed.
    (rule zero-observe-semantic-step-pending
      (head
        (zero-observe
          (zero-step-request
            ?request-occurrence ?subject ?continuation)
          ?bag
          (zero-step-pending-evidence
            ?request-occurrence ?producer-evidence)
          (q-app
            (q-sym (q-str "zero-pending"))
            (q-cons ?result (q-cons ?continuation q-nil)))))
      (body
        (zero-produced-member
          ?bag (gslt-produced ?producer-evidence ?result))))

    ; Exact completed emptiness is the only route to continuation handling.
    ; Resource-limited production never reaches this observer.
    (rule zero-observe-semantic-step-completed
      (head
        (zero-observe
          (zero-step-request
            ?request-occurrence ?subject ?continuation)
          zero-produced-nil
          (zero-step-completed-evidence
            ?request-occurrence ?continuation)
          ?result))
      (body (zero-continue ?continuation ?subject ?result)))

    (rule zero-continue-halt
      (head
        (zero-continue
          (q-sym (q-str "zero-halt"))
          ?value
          (q-app
            (q-sym (q-str "zero-completed"))
            (q-cons ?value q-nil))))
      (body))

    (rule zero-continue-then
      (head
        (zero-continue
          (q-app
            (q-sym (q-str "zero-then"))
            (q-cons ?template (q-cons ?next-continuation q-nil)))
          ?value
          (q-app
            (q-sym (q-str "zero-pending"))
            (q-cons ?next-subject
              (q-cons ?next-continuation q-nil)))))
      (body
        (qmatch-substitute
          ?template
          (qmatch-env-cons q-zero ?value qmatch-env-nil)
          ?next-subject)))
  ))
