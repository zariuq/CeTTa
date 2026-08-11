; A derived finite handler over the interaction core.
;
; A plan is `end` or `(then site-name rest)`.  Each named site is resolved to
; an authenticated event before its continuation is entered.  The handler
; threads the residual budget and retains every event receipt.
;
;   (! (run revision budget source plan))
;
; This layer is intentionally separate from one-step interaction.  Other
; handlers may implement search, fair scheduling, recurrence, or attention
; without changing what constitutes an authorized event.
(gslt-presentation-v1 MettaInteractionSequenceHandlerV1
  (signature
    (operator interact-classify 3)
    (operator interact-event 9)
    (operator interact-produce 4)
    (operator interact-run-base-evidence 1)
    (operator interact-run-evidence 3)
    (operator interact-run-request 5)
    (operator interact-run-valid 10)
    (operator interact-spend 4)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1))
  (equations)
  (rewrites
    (rule interact-classify-run
      (head
        (interact-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons
              (q-app
                (q-sym (q-str "run"))
                (q-cons ?revision
                  (q-cons ?budget
                    (q-cons ?source (q-cons ?plan q-nil)))))
              q-nil))
          (interact-run-request
            ?request-occurrence ?revision ?budget ?source ?plan)))
      (body))

    (rule interact-run-end
      (head
        (interact-run-valid
          ?program ?request-occurrence ?revision
          ?budget ?source
          (q-sym (q-str "end"))
          (interact-run-base-evidence ?source)
          ?source ?budget
          (q-sym (q-str "end"))))
      (body))

    (rule interact-run-then
      (head
        (interact-run-valid
          ?program ?request-occurrence ?revision
          ?budget ?source
          (q-app
            (q-sym (q-str "then"))
            (q-cons ?site (q-cons ?rest-plan q-nil)))
          (interact-run-evidence
            ?event-evidence ?spend-evidence ?rest-evidence)
          ?result ?remaining
          (q-app
            (q-sym (q-str "trace"))
            (q-cons ?event (q-cons ?rest-trace q-nil)))))
      (body
        (interact-event
          ?program ?request-occurrence ?revision ?source
          ?event-evidence ?site ?next ?cost ?event)
        (interact-spend ?budget ?cost ?next-budget ?spend-evidence)
        (interact-run-valid
          ?program ?request-occurrence ?revision
          ?next-budget ?next ?rest-plan
          ?rest-evidence ?result ?remaining ?rest-trace)))

    (rule interact-produce-run
      (head
        (interact-produce
          ?program
          (interact-run-request
            ?request-occurrence ?revision ?budget ?source ?plan)
          ?evidence
          (q-app
            (q-sym (q-str "finished"))
            (q-cons ?result
              (q-cons ?remaining (q-cons ?trace q-nil))))))
      (body
        (interact-run-valid
          ?program ?request-occurrence ?revision
          ?budget ?source ?plan
          ?evidence ?result ?remaining ?trace)))
  ))
