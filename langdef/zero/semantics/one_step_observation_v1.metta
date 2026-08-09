; Optional public observation over the MeTTa Zero one-step relation.
;
; The core does not know about requests or result publication.  This extension
; views an ordinary (! subject) program atom as a request and reports every
; rule/context occurrence licensed by the core relation.
(gslt-presentation-v1 MettaZeroOneStepObservationV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1)
    (operator zero-observe 4)
    (operator zero-program-atom 3)
    (operator zero-step 4))
  (equations)
  (rewrites
    (rule zero-observe-request
      (head
        (zero-observe
          ?program ?request-occurrence ?step-occurrence ?result))
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
