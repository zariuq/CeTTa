; Optional public observation over the Subzero one-step relation.
;
; The core does not know about requests or result publication.  This extension
; views an ordinary (! subject) program atom as a request and reports every
; rule/context occurrence licensed by the core relation.
(gslt-presentation-v1 SubzeroOneStepObservationV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1)
    (operator subzero-observe 4)
    (operator subzero-program-atom 3)
    (operator subzero-step 4))
  (equations)
  (rewrites
    (rule subzero-observe-request
      (head
        (subzero-observe
          ?program ?request-occurrence ?step-occurrence ?result))
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
