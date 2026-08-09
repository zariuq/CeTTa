; First executable MeTTa Zero semantic candidate.
;
; Object atoms use the shared finite-Horn quotation ABI.  A program is a free
; occurrence bag whose payloads are ordinary quoted atoms.  The only payload
; interpreted as a rewrite rule has the shape (= pattern template).  Every
; step retains both the source occurrence and its contextual location.
(gslt-presentation-v1 MettaZeroFreeBagRewriteCoreV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-ground 1)
    (operator q-int 1)
    (operator q-str 1)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)

    (operator zero-at-root 2)
    (operator zero-bind 4)
    (operator zero-env-cons 3)
    (operator zero-in-argument 2)
    (operator zero-in-head 1)
    (operator zero-index-lt 2)
    (operator zero-lookup 3)
    (operator zero-match 4)
    (operator zero-match-list 4)
    (operator zero-program-atom 3)
    (operator zero-program-cons 3)
    (operator zero-root-step 4)
    (operator zero-rule-view 4)
    (operator zero-step 4)
    (operator zero-step-list 4)
    (operator zero-substitute 3)
    (operator zero-substitute-list 3))

  (equations)

  (rewrites
    ; The representation is a list only to encode the free occurrence bag.
    ; No rule exposes position or order; occurrence identity carries
    ; multiplicity.
    (rule zero-program-atom-here
      (head
        (zero-program-atom
          (zero-program-cons ?occurrence ?atom ?rest)
          ?occurrence
          ?atom))
      (body))
    (rule zero-program-atom-rest
      (head
        (zero-program-atom
          (zero-program-cons ?head-occurrence ?head-atom ?rest)
          ?occurrence
          ?atom))
      (body (zero-program-atom ?rest ?occurrence ?atom)))

    ; Rules are not a privileged storage class.  They are ordinary program
    ; atoms for which this partial view succeeds.
    (rule zero-rule-view-equality
      (head
        (zero-rule-view ?program ?occurrence ?pattern ?template))
      (body
        (zero-program-atom
          ?program
          ?occurrence
          (q-app
            (q-sym (q-str "="))
            (q-cons ?pattern (q-cons ?template q-nil))))))

    ; Strict ordering on quoted de Bruijn indices supports a canonical finite
    ; substitution map without negation-as-failure.
    (rule zero-index-lt-base
      (head (zero-index-lt q-zero (q-succ ?right)))
      (body))
    (rule zero-index-lt-step
      (head
        (zero-index-lt (q-succ ?left) (q-succ ?right)))
      (body (zero-index-lt ?left ?right)))

    (rule zero-bind-empty
      (head
        (zero-bind
          ?index ?value zero-env-nil
          (zero-env-cons ?index ?value zero-env-nil)))
      (body))
    (rule zero-bind-existing
      (head
        (zero-bind
          ?index ?value
          (zero-env-cons ?index ?value ?rest)
          (zero-env-cons ?index ?value ?rest)))
      (body))
    (rule zero-bind-before
      (head
        (zero-bind
          ?index ?value
          (zero-env-cons ?next-index ?next-value ?rest)
          (zero-env-cons
            ?index ?value
            (zero-env-cons ?next-index ?next-value ?rest))))
      (body (zero-index-lt ?index ?next-index)))
    (rule zero-bind-after
      (head
        (zero-bind
          ?index ?value
          (zero-env-cons ?head-index ?head-value ?rest)
          (zero-env-cons ?head-index ?head-value ?updated)))
      (body
        (zero-index-lt ?head-index ?index)
        (zero-bind ?index ?value ?rest ?updated)))

    (rule zero-lookup-here
      (head
        (zero-lookup
          ?index
          (zero-env-cons ?index ?value ?rest)
          ?value))
      (body))
    (rule zero-lookup-after
      (head
        (zero-lookup
          ?index
          (zero-env-cons ?head-index ?head-value ?rest)
          ?value))
      (body
        (zero-index-lt ?head-index ?index)
        (zero-lookup ?index ?rest ?value)))

    ; Matching is structural.  Repeated object variables must reuse exactly
    ; the binding already stored at their quoted index.
    (rule zero-match-variable
      (head
        (zero-match (q-var ?index) ?value ?before ?after))
      (body (zero-bind ?index ?value ?before ?after)))
    (rule zero-match-symbol
      (head
        (zero-match (q-sym ?name) (q-sym ?name) ?env ?env))
      (body))
    (rule zero-match-integer
      (head
        (zero-match (q-int ?value) (q-int ?value) ?env ?env))
      (body))
    (rule zero-match-string
      (head
        (zero-match (q-str ?value) (q-str ?value) ?env ?env))
      (body))
    (rule zero-match-empty-expression
      (head (zero-match q-empty q-empty ?env ?env))
      (body))
    (rule zero-match-ground
      (head
        (zero-match
          (q-ground ?value) (q-ground ?value) ?env ?env))
      (body))
    (rule zero-match-application
      (head
        (zero-match
          (q-app ?pattern-head ?pattern-arguments)
          (q-app ?value-head ?value-arguments)
          ?before ?after))
      (body
        (zero-match ?pattern-head ?value-head ?before ?middle)
        (zero-match-list
          ?pattern-arguments ?value-arguments ?middle ?after)))
    (rule zero-match-list-empty
      (head (zero-match-list q-nil q-nil ?env ?env))
      (body))
    (rule zero-match-list-step
      (head
        (zero-match-list
          (q-cons ?pattern-head ?pattern-tail)
          (q-cons ?value-head ?value-tail)
          ?before ?after))
      (body
        (zero-match ?pattern-head ?value-head ?before ?middle)
        (zero-match-list
          ?pattern-tail ?value-tail ?middle ?after)))

    (rule zero-substitute-variable
      (head (zero-substitute (q-var ?index) ?env ?value))
      (body (zero-lookup ?index ?env ?value)))
    (rule zero-substitute-symbol
      (head
        (zero-substitute (q-sym ?name) ?env (q-sym ?name)))
      (body))
    (rule zero-substitute-integer
      (head
        (zero-substitute (q-int ?value) ?env (q-int ?value)))
      (body))
    (rule zero-substitute-string
      (head
        (zero-substitute (q-str ?value) ?env (q-str ?value)))
      (body))
    (rule zero-substitute-empty-expression
      (head (zero-substitute q-empty ?env q-empty))
      (body))
    (rule zero-substitute-ground
      (head
        (zero-substitute
          (q-ground ?value) ?env (q-ground ?value)))
      (body))
    (rule zero-substitute-application
      (head
        (zero-substitute
          (q-app ?head ?arguments)
          ?env
          (q-app ?result-head ?result-arguments)))
      (body
        (zero-substitute ?head ?env ?result-head)
        (zero-substitute-list ?arguments ?env ?result-arguments)))
    (rule zero-substitute-list-empty
      (head (zero-substitute-list q-nil ?env q-nil))
      (body))
    (rule zero-substitute-list-step
      (head
        (zero-substitute-list
          (q-cons ?head ?tail)
          ?env
          (q-cons ?result-head ?result-tail)))
      (body
        (zero-substitute ?head ?env ?result-head)
        (zero-substitute-list ?tail ?env ?result-tail)))

    (rule zero-root-step-from-rule-atom
      (head
        (zero-root-step
          ?program ?subject
          (zero-at-root ?occurrence ?environment)
          ?result))
      (body
        (zero-rule-view ?program ?occurrence ?pattern ?template)
        (zero-match
          ?pattern ?subject zero-env-nil ?environment)
        (zero-substitute ?template ?environment ?result)))

    ; Contextual closure chooses exactly one occurrence.  Root, expression
    ; head, and argument positions are distinguished by the occurrence proof.
    (rule zero-step-root
      (head
        (zero-step ?program ?subject ?occurrence ?result))
      (body
        (zero-root-step ?program ?subject ?occurrence ?result)))
    (rule zero-step-application-head
      (head
        (zero-step
          ?program
          (q-app ?head ?arguments)
          (zero-in-head ?occurrence)
          (q-app ?result-head ?arguments)))
      (body
        (zero-step ?program ?head ?occurrence ?result-head)))
    (rule zero-step-application-argument
      (head
        (zero-step
          ?program
          (q-app ?head ?arguments)
          ?occurrence
          (q-app ?head ?result-arguments)))
      (body
        (zero-step-list
          ?program ?arguments ?occurrence ?result-arguments)))
    (rule zero-step-list-head
      (head
        (zero-step-list
          ?program
          (q-cons ?head ?tail)
          (zero-in-argument q-zero ?occurrence)
          (q-cons ?result-head ?tail)))
      (body
        (zero-step ?program ?head ?occurrence ?result-head)))
    (rule zero-step-list-tail
      (head
        (zero-step-list
          ?program
          (q-cons ?head ?tail)
          (zero-in-argument (q-succ ?index) ?occurrence)
          (q-cons ?head ?result-tail)))
      (body
        (zero-step-list
          ?program
          ?tail
          (zero-in-argument ?index ?occurrence)
          ?result-tail)))
  ))
