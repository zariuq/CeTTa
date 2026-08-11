; Experimental MeTTa-shaped interaction candidate over an authenticated,
; revisioned space capability.  The authored GSLT owns composition and
; activation; physical providers own only open/candidate/emit leaf relations.
(gslt-presentation-v1 MettaZeroRevisionedInteractV1
  (signature
    (operator gslt-produced 2)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)
    (operator qmatch-index-lt 2)
    (operator qmatch-substitute-supported 4)
    (operator qmatch-supported-term 5)
    (operator zero-classify 3)
    (operator zero-compute 8)
    (operator zero-equation-view 3)
    (operator zero-eval-computation-evidence 4)
    (operator zero-evaluate-request 2)
    (operator zero-ground-apply 3)
    (operator zero-ground-computation-evidence 3)
    (operator zero-interact-observed-evidence 1)
    (operator zero-interact-production-evidence 4)
    (operator zero-interact-request 2)
    (operator zero-let-evidence 3)
    (operator zero-match-computation-evidence 4)
    (operator zero-observe 4)
    (operator zero-produce 4)
    (operator zero-produced-member 2)
    (operator zero-program-cons 3)
    (operator zero-quote-evidence 1)

    ; Provider relations: declared here, realized only through an admitted
    ; provider catalog bound to this exact generated language manifest.
    (operator zero-space-open 3)
    (operator zero-space-candidate 4)
    (operator zero-space-emit 6)
    (operator qabt-field-depth 4)
    (operator qabt-transport 4)

    ; Opaque but citable physical identities and receipts.
    (operator zero-world-token 1)
    (operator zero-space-stored-occurrence 2)
    (operator zero-space-emitted-occurrence 2)
    (operator zero-space-open-receipt 1)
    (operator zero-space-emit-receipt 5)
    (operator zero-emit-event 2)
    (operator zero-emit-computation-evidence 5))
  (equations)
  (rewrites
    (rule zero-classify-match-self
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "match"))
            (q-cons
              (q-sym (q-str "&self"))
              (q-cons ?pattern (q-cons ?template q-nil))))
          (zero-interact-request
            ?request-occurrence
            (q-app
              (q-sym (q-str "match"))
              (q-cons
                (q-sym (q-str "&self"))
                (q-cons ?pattern (q-cons ?template q-nil)))))))
      (body))
    (rule zero-classify-let
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "let"))
            (q-cons ?pattern (q-cons ?producer (q-cons ?body q-nil))))
          (zero-interact-request
            ?request-occurrence
            (q-app
              (q-sym (q-str "let"))
              (q-cons ?pattern
                (q-cons ?producer (q-cons ?body q-nil)))))))
      (body))
    (rule zero-classify-emit-self
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "emit"))
            (q-cons (q-sym (q-str "&self")) (q-cons ?atom q-nil)))
          (zero-interact-request
            ?request-occurrence
            (q-app
              (q-sym (q-str "emit"))
              (q-cons (q-sym (q-str "&self"))
                (q-cons ?atom q-nil))))))
      (body))
    (rule zero-classify-add-atom-self
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "add-atom"))
            (q-cons (q-sym (q-str "&self")) (q-cons ?atom q-nil)))
          (zero-interact-request
            ?request-occurrence
            (q-app
              (q-sym (q-str "add-atom"))
              (q-cons (q-sym (q-str "&self"))
                (q-cons ?atom q-nil))))))
      (body))
    (rule zero-classify-quote
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "quote"))
            (q-cons ?value q-nil))
          (zero-interact-request
            ?request-occurrence
            (q-app
              (q-sym (q-str "quote"))
              (q-cons ?value q-nil)))))
      (body))

    (rule zero-compute-quote
      (head
        (zero-compute
          ?world ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "quote"))
            (q-cons ?value q-nil))
          (zero-quote-evidence ?revision)
          ?world ?revision ?value))
      (body))

    ; The physical provider performs only an indexed candidate selection over
    ; the quoted pattern.  The authored qmatch below remains the semantic
    ; authority and rejects any candidate outside the exact one-way relation.
    (rule zero-compute-match-self
      (head
        (zero-compute
          ?world ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "match"))
            (q-cons
              (q-sym (q-str "&self"))
              (q-cons ?pattern (q-cons ?template q-nil))))
          (zero-match-computation-evidence
            ?world ?revision ?candidate-occurrence ?environment)
          ?world ?revision ?result))
      (body
        (zero-space-candidate
          ?world ?pattern ?candidate-occurrence ?candidate)
        (qmatch-supported-term
          ?pattern ?candidate q-zero qmatch-supported-env-nil ?environment)
        (qmatch-substitute-supported
          ?template ?environment q-zero ?result)))

    ; Activation queries equations in the current world.  It is one
    ; productive evaluation step, not a hidden normalization loop.
    (rule zero-compute-eval-equation
      (head
        (zero-compute
          ?world ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "eval"))
            (q-cons ?subject q-nil))
          (zero-eval-computation-evidence
            ?world ?revision ?candidate-occurrence ?environment)
          ?world ?revision ?result))
      (body
        (zero-space-candidate
          ?world
          (q-app
            (q-sym (q-str "="))
            (q-cons
              (q-var q-zero)
              (q-cons (q-var (q-succ q-zero)) q-nil)))
          ?candidate-occurrence ?candidate)
        (zero-equation-view ?candidate ?left ?right)
        (qmatch-supported-term
          ?left ?subject q-zero qmatch-supported-env-nil ?environment)
        (qmatch-substitute-supported
          ?right ?environment q-zero ?result)))
    (rule zero-compute-eval-ground
      (head
        (zero-compute
          ?world ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "eval"))
            (q-cons ?subject q-nil))
          (zero-ground-computation-evidence
            ?world ?revision ?ground-evidence)
          ?world ?revision ?result))
      (body
        (zero-ground-apply ?subject ?ground-evidence ?result)))

    (rule zero-compute-emit-self
      (head
        (zero-compute
          ?world-before ?request-occurrence ?revision-before
          (q-app
            (q-sym (q-str "emit"))
            (q-cons (q-sym (q-str "&self")) (q-cons ?atom q-nil)))
          (zero-emit-computation-evidence
            ?revision-before (q-succ ?revision-before)
            ?emitted-occurrence ?provider-receipt ?atom)
          ?world-after (q-succ ?revision-before) q-empty))
      (body
        (zero-space-emit
          ?world-before
          (zero-emit-event ?request-occurrence ?revision-before)
          ?atom ?world-after ?emitted-occurrence ?provider-receipt)))

    ; Compatibility spelling is exactly the same provider relation and
    ; evidence, not a second mutation operation.
    (rule zero-compute-add-atom-self
      (head
        (zero-compute
          ?world-before ?request-occurrence ?revision-before
          (q-app
            (q-sym (q-str "add-atom"))
            (q-cons (q-sym (q-str "&self")) (q-cons ?atom q-nil)))
          (zero-emit-computation-evidence
            ?revision-before (q-succ ?revision-before)
            ?emitted-occurrence ?provider-receipt ?atom)
          ?world-after (q-succ ?revision-before) q-empty))
      (body
        (zero-space-emit
          ?world-before
          (zero-emit-event ?request-occurrence ?revision-before)
          ?atom ?world-after ?emitted-occurrence ?provider-receipt)))

    (rule zero-compute-let
      (head
        (zero-compute
          ?world-before ?request-occurrence ?revision-before
          (q-app
            (q-sym (q-str "let"))
            (q-cons ?pattern
              (q-cons ?producer (q-cons ?body q-nil))))
          (zero-let-evidence
            ?producer-evidence ?environment ?continuation-evidence)
          ?world-after ?revision-after ?result))
      (body
        (zero-compute
          ?world-before ?request-occurrence ?revision-before
          ?producer ?producer-evidence
          ?world-middle ?revision-middle ?value)
        (qmatch-supported-term
          ?pattern ?value q-zero qmatch-supported-env-nil ?environment)
        (qmatch-substitute-supported
          ?body ?environment q-zero ?instantiated-body)
        (zero-compute
          ?world-middle ?request-occurrence ?revision-middle
          ?instantiated-body ?continuation-evidence
          ?world-after ?revision-after ?result)))

    ; Opening the document is itself an authenticated physical event.  The
    ; resulting world token is opaque to authored computation but retained in
    ; the production article.
    (rule zero-produce-interact
      (head
        (zero-produce
          ?program
          (zero-interact-request ?request-occurrence ?computation)
          (zero-interact-production-evidence
            ?open-receipt ?computation-evidence
            ?world-after ?revision-after)
          ?result))
      (body
        (zero-space-open ?program ?world-before ?open-receipt)
        (zero-compute
          ?world-before ?request-occurrence q-zero ?computation
          ?computation-evidence ?world-after ?revision-after ?result)))

    (rule zero-observe-interact
      (head
        (zero-observe
          (zero-interact-request ?request-occurrence ?computation)
          ?bag
          (zero-interact-observed-evidence ?producer-evidence)
          ?result))
      (body
        (zero-produced-member
          ?bag (gslt-produced ?producer-evidence ?result))))
  ))
