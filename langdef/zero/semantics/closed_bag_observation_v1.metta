; Observation is a separate authored stage.  The generic runner invokes it
; only after the positive producer has completed and packages every produced
; occurrence into the declared bag carrier.
(gslt-presentation-v1 MettaZeroClosedBagObservationV1
  (signature
    (operator gslt-produced 2)
    (operator zero-evaluate-request 2)
    (operator zero-inert-evidence 1)
    (operator zero-observe 4)
    (operator zero-observed-evidence 1)
    (operator zero-produced-cons 2)
    (operator zero-produced-member 2)
    (operator zero-query-request 3))
  (equations)
  (rewrites
    (rule zero-produced-member-here
      (head
        (zero-produced-member
          (zero-produced-cons ?item ?rest) ?item))
      (body))
    (rule zero-produced-member-rest
      (head
        (zero-produced-member
          (zero-produced-cons ?head ?rest) ?item))
      (body (zero-produced-member ?rest ?item)))

    ; An empty query has no answers.  A nonempty query preserves one output
    ; per producer occurrence, including duplicates.
    (rule zero-observe-query-result
      (head
        (zero-observe
          (zero-query-request
            ?request-occurrence ?pattern ?template)
          ?bag
          (zero-observed-evidence ?producer-evidence)
          ?result))
      (body
        (zero-produced-member
          ?bag (gslt-produced ?producer-evidence ?result))))

    ; Known evaluation results retain their producer evidence.
    (rule zero-observe-evaluation-result
      (head
        (zero-observe
          (zero-evaluate-request ?request-occurrence ?subject)
          ?bag
          (zero-observed-evidence ?producer-evidence)
          ?result))
      (body
        (zero-produced-member
          ?bag (gslt-produced ?producer-evidence ?result))))

    ; Exact completed emptiness means ignorance.  It retains the subject as
    ; inert data; an open or resource-exhausted producer never reaches here.
    (rule zero-observe-inert-evaluation
      (head
        (zero-observe
          (zero-evaluate-request ?request-occurrence ?subject)
          zero-produced-nil
          (zero-inert-evidence ?request-occurrence)
          ?subject))
      (body))
  ))
