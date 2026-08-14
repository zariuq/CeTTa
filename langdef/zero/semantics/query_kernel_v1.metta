; Query-first MeTTa Zero executable candidate over one reflective occurrence
; space.  The syntax `(zero-query pattern template)` publishes arbitrary
; matching.  Evaluation requests `(eval subject)` enumerate that same space,
; select ordinary equation atoms, and apply their right sides.
(gslt-presentation-v1 MettaZeroQueryKernelV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1)
    (operator q-var 1)
    (operator qmatch-substitute 3)
    (operator qmatch-term 4)
    (operator zero-classify 3)
    (operator zero-equation-evidence 4)
    (operator zero-equation-view 3)
    (operator zero-evaluate-request 2)
    (operator zero-ground-apply 3)
    (operator zero-ground-evidence 2)
    (operator zero-produce 4)
    (operator zero-program-atom 3)
    (operator zero-program-cons 3)
    (operator zero-public-query 8)
    (operator zero-query-evidence 3)
    (operator zero-query-request 3)
    (operator zero-reify-evaluate-request 2)
    (operator zero-reify-query-request 3))
  (equations)
  (rewrites
    ; Request classification is authored and exhaustive.  Forms not
    ; classified here remain ordinary atoms in the reflective space.
    (rule zero-classify-query
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "zero-query"))
            (q-cons ?pattern (q-cons ?template q-nil)))
          (zero-query-request
            ?request-occurrence ?pattern ?template)))
      (body))
    (rule zero-classify-evaluate
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "eval"))
            (q-cons ?subject q-nil))
          (zero-evaluate-request ?request-occurrence ?subject)))
      (body))

    ; Reification eliminates one already-declared computation request.  Its
    ; result is one expression containing every visible occurrence.  The
    ; element order is a realization-chosen traversal witness; the language
    ; promises multiplicity and membership, not an ordering law.
    (rule zero-classify-reify-query
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "reify"))
            (q-cons
              (q-app
                (q-sym (q-str "zero-query"))
                (q-cons ?pattern (q-cons ?template q-nil)))
              q-nil))
          (zero-reify-query-request
            ?request-occurrence ?pattern ?template)))
      (body))
    (rule zero-classify-reify-evaluate
      (head
        (zero-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "reify"))
            (q-cons
              (q-app
                (q-sym (q-str "eval"))
                (q-cons ?subject q-nil))
              q-nil))
          (zero-reify-evaluate-request
            ?request-occurrence ?subject)))
      (body))

    ; The list represents an occurrence bag only.  No rule exposes its
    ; position; the explicit occurrence label carries multiplicity.
    (rule zero-program-atom-here
      (head
        (zero-program-atom
          (zero-program-cons ?occurrence ?atom ?rest)
          ?occurrence ?atom))
      (body))
    (rule zero-program-atom-rest
      (head
        (zero-program-atom
          (zero-program-cons ?head-occurrence ?head-atom ?rest)
          ?occurrence ?atom))
      (body (zero-program-atom ?rest ?occurrence ?atom)))

    ; This is the public symbolic engine.  Facts, rules, types, and other
    ; knowledge atoms all pass through the identical query relation.
    (rule zero-public-query-from-atom
      (head
        (zero-public-query
          ?program ?request-occurrence ?pattern ?template
          ?candidate-occurrence ?environment ?result
          (zero-query-evidence
            ?request-occurrence ?candidate-occurrence ?environment)))
      (body
        (zero-program-atom ?program ?candidate-occurrence ?candidate)
        (qmatch-term
          ?pattern ?candidate qmatch-env-nil ?environment)
        (qmatch-substitute ?template ?environment ?result)))

    (rule zero-equation-view-ordinary-atom
      (head
        (zero-equation-view
          (q-app
            (q-sym (q-str "="))
            (q-cons ?left (q-cons ?right q-nil)))
          ?left ?right))
      (body))

    (rule zero-produce-query
      (head
        (zero-produce
          ?program
          (zero-query-request
            ?request-occurrence ?pattern ?template)
          ?evidence ?result))
      (body
        (zero-public-query
          ?program ?request-occurrence ?pattern ?template
          ?candidate-occurrence ?environment ?result ?evidence)))

    ; Reification changes only the observation of a request.  Production is
    ; delegated to the same public query/evaluation relations, so it cannot
    ; become a second evaluator.
    (rule zero-produce-reify-query
      (head
        (zero-produce
          ?program
          (zero-reify-query-request
            ?request-occurrence ?pattern ?template)
          ?evidence ?result))
      (body
        (zero-produce
          ?program
          (zero-query-request
            ?request-occurrence ?pattern ?template)
          ?evidence ?result)))
    (rule zero-produce-reify-evaluate
      (head
        (zero-produce
          ?program
          (zero-reify-evaluate-request
            ?request-occurrence ?subject)
          ?evidence ?result))
      (body
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          ?evidence ?result)))

    ; Evaluation obtains every candidate equation through public query using
    ; a reserved quoted wildcard, then invokes the same structural matcher on
    ; the equation's left side.  There is no private rule store.
    (rule zero-produce-equation
      (head
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          (zero-equation-evidence
            ?request-occurrence ?candidate-occurrence
            ?query-environment ?equation-environment)
          ?result))
      (body
        (zero-public-query
          ?program ?request-occurrence
          (q-var q-zero) (q-var q-zero)
          ?candidate-occurrence ?query-environment ?candidate
          ?query-evidence)
        (zero-equation-view ?candidate ?left ?right)
        (qmatch-term
          ?left ?subject qmatch-env-nil ?equation-environment)
        (qmatch-substitute ?right ?equation-environment ?result)))

    ; `zero-ground-apply` is an open relation supplied by an explicit
    ; capability extension.  With no such rules the executable model declines
    ; grounding and the closed-bag observer applies inertness.
    (rule zero-produce-ground
      (head
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          (zero-ground-evidence ?request-occurrence ?ground-evidence)
          ?result))
      (body (zero-ground-apply ?subject ?ground-evidence ?result)))
  ))
