; Experimental revision-threaded interaction layer for Zero.  Computations
; are ordinary terms.  `match` reads the current reflective occurrence space,
; `let` composes answer-producing computations, `eval` activates the existing
; query-derived evaluator, and `emit` publishes one occurrence for subsequent
; work in the same transaction.  `add-atom` elaborates to the identical
; persistent emission relation.
(gslt-presentation-v1 MettaZeroRevisionedEmitV1
  (signature
    (operator gslt-produced 2)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)
    (operator qmatch-env-cons 3)
    (operator qmatch-index-lt 2)
    (operator qmatch-open-variable 3)
    (operator qmatch-substitute-open 3)
    (operator qmatch-substitute-open-list 3)
    (operator qmatch-substitute 3)
    (operator qmatch-term 4)
    (operator zero-classify 3)
    (operator zero-compute 8)
    (operator zero-emitted-occurrence 2)
    (operator zero-emit-evidence 5)
    (operator zero-evaluate-request 2)
    (operator zero-eval-computation-evidence 2)
    (operator zero-interact-observed-evidence 1)
    (operator zero-interact-production-evidence 3)
    (operator zero-interact-request 2)
    (operator zero-let-evidence 3)
    (operator zero-match-computation-evidence 2)
    (operator zero-observe 4)
    (operator zero-produce 4)
    (operator zero-produced-member 2)
    (operator zero-program-cons 3)
    (operator zero-public-query 8)
    (operator zero-quote-evidence 1))
  (equations)
  (rewrites
    ; Only the single reflective space is admitted in this first profile.
    ; Other targets remain unclassified data instead of producing an error.
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

    ; Open continuation substitution replaces variables bound by the current
    ; producer and preserves variables belonging to later nested operations.
    ; Environments are ordered by the shared matcher, so absence is decidable
    ; constructively by reaching the empty environment or passing the index.
    (rule qmatch-open-variable-empty
      (head
        (qmatch-open-variable
          ?index qmatch-env-nil (q-var ?index)))
      (body))
    (rule qmatch-open-variable-here
      (head
        (qmatch-open-variable
          ?index
          (qmatch-env-cons ?index ?value ?rest)
          ?value))
      (body))
    (rule qmatch-open-variable-before
      (head
        (qmatch-open-variable
          ?index
          (qmatch-env-cons ?next-index ?next-value ?rest)
          (q-var ?index)))
      (body (qmatch-index-lt ?index ?next-index)))
    (rule qmatch-open-variable-after
      (head
        (qmatch-open-variable
          ?index
          (qmatch-env-cons ?head-index ?head-value ?rest)
          ?result))
      (body
        (qmatch-index-lt ?head-index ?index)
        (qmatch-open-variable ?index ?rest ?result)))

    (rule qmatch-substitute-open-variable
      (head
        (qmatch-substitute-open
          (q-var ?index) ?environment ?result))
      (body
        (qmatch-open-variable ?index ?environment ?result)))
    (rule qmatch-substitute-open-symbol
      (head
        (qmatch-substitute-open
          (q-sym ?name) ?environment (q-sym ?name)))
      (body))
    (rule qmatch-substitute-open-integer
      (head
        (qmatch-substitute-open
          (q-int ?value) ?environment (q-int ?value)))
      (body))
    (rule qmatch-substitute-open-string
      (head
        (qmatch-substitute-open
          (q-str ?value) ?environment (q-str ?value)))
      (body))
    (rule qmatch-substitute-open-empty-expression
      (head
        (qmatch-substitute-open q-empty ?environment q-empty))
      (body))
    (rule qmatch-substitute-open-ground
      (head
        (qmatch-substitute-open
          (q-ground ?value) ?environment (q-ground ?value)))
      (body))
    (rule qmatch-substitute-open-application
      (head
        (qmatch-substitute-open
          (q-app ?head ?arguments) ?environment
          (q-app ?result-head ?result-arguments)))
      (body
        (qmatch-substitute-open ?head ?environment ?result-head)
        (qmatch-substitute-open-list
          ?arguments ?environment ?result-arguments)))
    (rule qmatch-substitute-open-list-empty
      (head
        (qmatch-substitute-open-list q-nil ?environment q-nil))
      (body))
    (rule qmatch-substitute-open-list-step
      (head
        (qmatch-substitute-open-list
          (q-cons ?head ?tail) ?environment
          (q-cons ?result-head ?result-tail)))
      (body
        (qmatch-substitute-open ?head ?environment ?result-head)
        (qmatch-substitute-open-list
          ?tail ?environment ?result-tail)))

    ; Quotation is the pure computation.  It does not activate its payload.
    (rule zero-compute-quote
      (head
        (zero-compute
          ?program ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "quote"))
            (q-cons ?value q-nil))
          (zero-quote-evidence ?revision)
          ?program ?revision ?value))
      (body))

    ; Named-space matching is read-only and retains the public query evidence,
    ; including the matched atom occurrence and substitution environment.
    (rule zero-compute-match-self
      (head
        (zero-compute
          ?program ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "match"))
            (q-cons
              (q-sym (q-str "&self"))
              (q-cons ?pattern (q-cons ?template q-nil))))
          (zero-match-computation-evidence ?revision ?query-evidence)
          ?program ?revision ?result))
      (body
        (zero-public-query
          ?program ?request-occurrence ?pattern ?template
          ?candidate-occurrence ?environment ?result ?query-evidence)))

    ; Activation reuses Zero's public query-derived evaluator.  This relation
    ; is deliberately untotalized inside `let`: exact empty production is an
    ; observer/handler concern rather than a fabricated productive answer.
    (rule zero-compute-eval
      (head
        (zero-compute
          ?program ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "eval"))
            (q-cons ?subject q-nil))
          (zero-eval-computation-evidence ?revision ?eval-evidence)
          ?program ?revision ?result))
      (body
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          ?eval-evidence ?result)))

    ; Persistent emission prepends one occurrence carrying a stable semantic
    ; identity derived from the request and predecessor revision.
    (rule zero-compute-emit-self
      (head
        (zero-compute
          ?program ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "emit"))
            (q-cons (q-sym (q-str "&self")) (q-cons ?atom q-nil)))
          (zero-emit-evidence
            ?request-occurrence ?revision (q-succ ?revision)
            (zero-emitted-occurrence ?request-occurrence ?revision) ?atom)
          (zero-program-cons
            (zero-emitted-occurrence ?request-occurrence ?revision)
            ?atom ?program)
          (q-succ ?revision)
          q-empty))
      (body))

    ; Compatibility spelling: exactly the same endpoint and evidence as
    ; persistent `emit`, rather than a second mutation semantics.
    (rule zero-compute-add-atom-self
      (head
        (zero-compute
          ?program ?request-occurrence ?revision
          (q-app
            (q-sym (q-str "add-atom"))
            (q-cons (q-sym (q-str "&self")) (q-cons ?atom q-nil)))
          (zero-emit-evidence
            ?request-occurrence ?revision (q-succ ?revision)
            (zero-emitted-occurrence ?request-occurrence ?revision) ?atom)
          (zero-program-cons
            (zero-emitted-occurrence ?request-occurrence ?revision)
            ?atom ?program)
          (q-succ ?revision)
          q-empty))
      (body))

    ; `let` is patterned proof-relevant Kleisli composition.  The producer's
    ; successor program and revision become the continuation's input.
    (rule zero-compute-let
      (head
        (zero-compute
          ?program-before ?request-occurrence ?revision-before
          (q-app
            (q-sym (q-str "let"))
            (q-cons ?pattern
              (q-cons ?producer (q-cons ?body q-nil))))
          (zero-let-evidence
            ?producer-evidence ?environment ?continuation-evidence)
          ?program-after ?revision-after ?result))
      (body
        (zero-compute
          ?program-before ?request-occurrence ?revision-before
          ?producer ?producer-evidence
          ?program-middle ?revision-middle ?value)
        (qmatch-term
          ?pattern ?value qmatch-env-nil ?environment)
        (qmatch-substitute-open ?body ?environment ?instantiated-body)
        (zero-compute
          ?program-middle ?request-occurrence ?revision-middle
          ?instantiated-body ?continuation-evidence
          ?program-after ?revision-after ?result)))

    ; The request pipeline begins one transaction at revision zero.  Final
    ; state remains in proof evidence even when the public answer is plain
    ; MeTTa data.
    (rule zero-produce-interact
      (head
        (zero-produce
          ?program
          (zero-interact-request ?request-occurrence ?computation)
          (zero-interact-production-evidence
            ?computation-evidence ?program-after ?revision-after)
          ?result))
      (body
        (zero-compute
          ?program ?request-occurrence q-zero ?computation
          ?computation-evidence ?program-after ?revision-after ?result)))

    ; Every productive branch remains a separate public answer occurrence.
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
