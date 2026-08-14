; Experimental proof-relevant interaction core.
;
; Authored sites have the form:
;   (site revision name pattern template cost)
;
; Cost is a Peano natural: z or (s cost).  A site occurrence authorizes one
; pattern-matched rewrite event.  The selected revision, site name, exact
; program occurrence, substitution, endpoints, and cost remain available in
; the internal evidence; the public event retains everything except the
; substitution environment.
;
; Syntax requests:
;   (! (events revision source))
;   (! (fire revision budget source site-name))
;
; `fire` does not run a hidden loop.  It consumes one authenticated event and
; returns a continuation carrying its target, residual budget, and receipt.
(gslt-presentation-v1 MettaInteractionCoreV1
  (signature
    (operator gslt-produced 2)
    (operator gslt-source-occurrence 1)
    (operator interact-classify 3)
    (operator interact-continue-evidence 2)
    (operator interact-event 9)
    (operator interact-event-evidence 3)
    (operator interact-events-request 3)
    (operator interact-fire-request 5)
    (operator interact-observe 4)
    (operator interact-observed-evidence 1)
    (operator interact-occurrence-quote 2)
    (operator interact-produce 4)
    (operator interact-produced-cons 2)
    (operator interact-produced-member 2)
    (operator interact-program-atom 3)
    (operator interact-program-cons 3)
    (operator interact-site-view 6)
    (operator interact-spend 4)
    (operator interact-spend-base-evidence 1)
    (operator interact-spend-step-evidence 2)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-int 1)
    (operator q-str 1)
    (operator q-sym 1)
    (operator qmatch-substitute 3)
    (operator qmatch-term 4))
  (equations)
  (rewrites
    (rule interact-classify-events
      (head
        (interact-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons
              (q-app
                (q-sym (q-str "events"))
                (q-cons ?revision (q-cons ?source q-nil)))
              q-nil))
          (interact-events-request
            ?request-occurrence ?revision ?source)))
      (body))

    (rule interact-classify-fire
      (head
        (interact-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons
              (q-app
                (q-sym (q-str "fire"))
                (q-cons ?revision
                  (q-cons ?budget
                    (q-cons ?source (q-cons ?site q-nil)))))
              q-nil))
          (interact-fire-request
            ?request-occurrence ?revision ?budget ?source ?site)))
      (body))

    ; Program occurrences, rather than values alone, witness site identity.
    (rule interact-program-atom-here
      (head
        (interact-program-atom
          (interact-program-cons ?occurrence ?atom ?rest)
          ?occurrence ?atom))
      (body))
    (rule interact-program-atom-rest
      (head
        (interact-program-atom
          (interact-program-cons ?head-occurrence ?head-atom ?rest)
          ?occurrence ?atom))
      (body (interact-program-atom ?rest ?occurrence ?atom)))

    (rule interact-site-view-atom
      (head
        (interact-site-view
          (q-app
            (q-sym (q-str "site"))
            (q-cons ?revision
              (q-cons ?site
                (q-cons ?pattern
                  (q-cons ?template (q-cons ?cost q-nil))))))
          ?revision ?site ?pattern ?template ?cost))
      (body))

    (rule interact-occurrence-quote-source
      (head
        (interact-occurrence-quote
          (gslt-source-occurrence ?index) (q-int ?index)))
      (body))

    ; The universal authored seam: selected rewrite occurrence,
    ; substitution, and source determine exactly one target.
    (rule interact-event-from-site
      (head
        (interact-event
          ?program ?request-occurrence ?revision ?source
          (interact-event-evidence
            ?request-occurrence ?site-occurrence ?environment)
          ?site ?target ?cost
          (q-app
            (q-sym (q-str "event"))
            (q-cons ?revision
              (q-cons ?site
                (q-cons ?occurrence-label
                  (q-cons ?source
                    (q-cons ?target (q-cons ?cost q-nil)))))))))
      (body
        (interact-program-atom ?program ?site-occurrence ?candidate)
        (interact-occurrence-quote ?site-occurrence ?occurrence-label)
        (interact-site-view
          ?candidate ?revision ?site ?pattern ?template ?cost)
        (qmatch-term ?pattern ?source qmatch-env-nil ?environment)
        (qmatch-substitute ?template ?environment ?target)))

    (rule interact-produce-events
      (head
        (interact-produce
          ?program
          (interact-events-request
            ?request-occurrence ?revision ?source)
          ?evidence ?event))
      (body
        (interact-event
          ?program ?request-occurrence ?revision ?source
          ?evidence ?site ?target ?cost ?event)))

    ; Cost is an event-indexed admission fibre.  It neither creates nor
    ; alters the semantic endpoint selected above.
    (rule interact-spend-zero
      (head
        (interact-spend
          ?budget (q-sym (q-str "z")) ?budget
          (interact-spend-base-evidence ?budget)))
      (body))
    (rule interact-spend-successor
      (head
        (interact-spend
          (q-app
            (q-sym (q-str "s"))
            (q-cons ?budget q-nil))
          (q-app
            (q-sym (q-str "s"))
            (q-cons ?cost q-nil))
          ?remaining
          (interact-spend-step-evidence ?budget ?rest-evidence)))
      (body (interact-spend ?budget ?cost ?remaining ?rest-evidence)))

    (rule interact-produce-fire
      (head
        (interact-produce
          ?program
          (interact-fire-request
            ?request-occurrence ?revision ?budget ?source ?site)
          (interact-continue-evidence ?event-evidence ?spend-evidence)
          (q-app
            (q-sym (q-str "continue"))
            (q-cons ?target
              (q-cons ?remaining (q-cons ?event q-nil))))))
      (body
        (interact-event
          ?program ?request-occurrence ?revision ?source
          ?event-evidence ?site ?target ?cost ?event)
        (interact-spend ?budget ?cost ?remaining ?spend-evidence)))

    ; Bag observation exposes one result per proof-relevant producer
    ; occurrence.  Empty production and unknown commands remain empty.
    (rule interact-produced-member-here
      (head
        (interact-produced-member
          (interact-produced-cons ?item ?rest) ?item))
      (body))
    (rule interact-produced-member-rest
      (head
        (interact-produced-member
          (interact-produced-cons ?head ?rest) ?item))
      (body (interact-produced-member ?rest ?item)))
    (rule interact-observe-result
      (head
        (interact-observe
          ?request ?bag
          (interact-observed-evidence ?producer-evidence)
          ?result))
      (body
        (interact-produced-member
          ?bag (gslt-produced ?producer-evidence ?result))))
  ))
