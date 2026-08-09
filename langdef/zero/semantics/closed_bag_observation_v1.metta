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
    (operator zero-query-request 3)
    (operator zero-reified-evidence 2)
    (operator zero-reified-inert-evidence 1)
    (operator zero-reify-evaluate-request 2)
    (operator zero-reify-produced 2)
    (operator zero-reify-produced-tail 2)
    (operator zero-reify-query-request 3))
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

    ; A completed produced bag is reified in one pass.  This is deliberately
    ; a traversal witness rather than a canonical sort: all occurrences and
    ; their multiplicities are retained, while order remains unspecified.
    (rule zero-reify-produced-empty
      (head (zero-reify-produced zero-produced-nil q-empty))
      (body))
    (rule zero-reify-produced-nonempty
      (head
        (zero-reify-produced
          (zero-produced-cons
            (gslt-produced ?head-evidence ?head-result) ?rest)
          (q-app ?head-result ?tail-results)))
      (body (zero-reify-produced-tail ?rest ?tail-results)))
    (rule zero-reify-produced-tail-empty
      (head
        (zero-reify-produced-tail zero-produced-nil q-nil))
      (body))
    (rule zero-reify-produced-tail-step
      (head
        (zero-reify-produced-tail
          (zero-produced-cons
            (gslt-produced ?head-evidence ?head-result) ?rest)
          (q-cons ?head-result ?tail-results)))
      (body (zero-reify-produced-tail ?rest ?tail-results)))

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

    ; Query reification exposes one finite collection datum, including the
    ; empty expression when the query has no answers.  The evidence retains
    ; the complete producer bag, including occurrence identities.
    (rule zero-observe-reified-query
      (head
        (zero-observe
          (zero-reify-query-request
            ?request-occurrence ?pattern ?template)
          ?bag
          (zero-reified-evidence ?request-occurrence ?bag)
          ?result))
      (body (zero-reify-produced ?bag ?result)))

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

    ; A successful evaluation reifies the same occurrences as ordinary
    ; evaluation.  Exact empty production is different: ordinary evaluation
    ; returns the unknown subject inertly, so its reification is a singleton.
    (rule zero-observe-reified-evaluation-result
      (head
        (zero-observe
          (zero-reify-evaluate-request
            ?request-occurrence ?subject)
          (zero-produced-cons ?head ?rest)
          (zero-reified-evidence
            ?request-occurrence (zero-produced-cons ?head ?rest))
          ?result))
      (body
        (zero-reify-produced
          (zero-produced-cons ?head ?rest) ?result)))
    (rule zero-observe-reified-inert-evaluation
      (head
        (zero-observe
          (zero-reify-evaluate-request
            ?request-occurrence ?subject)
          zero-produced-nil
          (zero-reified-inert-evidence ?request-occurrence)
          (q-app ?subject q-nil)))
      (body))

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
