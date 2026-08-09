; Public result-bag observation for the first MeTTa Zero candidate.
;
; Rule and context occurrences remain present through this relation, so a
; set-valued intermediate executor cannot collapse equal result payloads.
; The final observation projection erases their spellings while retaining the
; occurrence carrier as a bag.
(gslt-presentation-v1 MettaZeroPublicResultBagV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1)
    (operator zero-evaluate 3)
    (operator zero-program-atom 3)
    (operator zero-result-occurrence 2)
    (operator zero-step 4))
  (equations)
  (rewrites
    (rule zero-evaluate-request
      (head
        (zero-evaluate
          ?program
          (zero-result-occurrence
            ?request-occurrence ?step-occurrence)
          ?result))
      (body
        (zero-program-atom
          ?program
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons ?subject q-nil)))
        (zero-step
          ?program ?subject ?step-occurrence ?result)))
  ))
