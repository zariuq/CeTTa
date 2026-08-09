; Public result-bag observation for the first Subzero candidate.
;
; Rule and context occurrences remain present through this relation, so a
; set-valued intermediate executor cannot collapse equal result payloads.
; The final observation projection erases their spellings while retaining the
; occurrence carrier as a bag.
(gslt-presentation-v1 SubzeroPublicResultBagV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1)
    (operator subzero-evaluate 3)
    (operator subzero-program-atom 3)
    (operator subzero-result-occurrence 2)
    (operator subzero-step 4))
  (equations)
  (rewrites
    (rule subzero-evaluate-request
      (head
        (subzero-evaluate
          ?program
          (subzero-result-occurrence
            ?request-occurrence ?step-occurrence)
          ?result))
      (body
        (subzero-program-atom
          ?program
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons ?subject q-nil)))
        (subzero-step
          ?program ?subject ?step-occurrence ?result)))
  ))
