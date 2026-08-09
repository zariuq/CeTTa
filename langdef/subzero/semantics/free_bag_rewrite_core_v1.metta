; First executable Subzero semantic candidate.
;
; Object atoms use the shared finite-Horn quotation ABI.  A program is a free
; occurrence bag whose payloads are ordinary quoted atoms.  The only payload
; interpreted as a rewrite rule has the shape (= pattern template).  Every
; step retains both the source occurrence and its contextual location.
(gslt-presentation-v1 SubzeroFreeBagRewriteCoreV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-ground 1)
    (operator q-int 1)
    (operator q-str 1)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)

    (operator subzero-at-root 2)
    (operator subzero-bind 4)
    (operator subzero-env-cons 3)
    (operator subzero-in-argument 2)
    (operator subzero-in-head 1)
    (operator subzero-index-lt 2)
    (operator subzero-lookup 3)
    (operator subzero-match 4)
    (operator subzero-match-list 4)
    (operator subzero-program-atom 3)
    (operator subzero-program-cons 3)
    (operator subzero-root-step 4)
    (operator subzero-rule-view 4)
    (operator subzero-step 4)
    (operator subzero-step-list 4)
    (operator subzero-substitute 3)
    (operator subzero-substitute-list 3))

  (equations)

  (rewrites
    ; The representation is a list only to encode the free occurrence bag.
    ; No rule exposes position or order; occurrence identity carries
    ; multiplicity.
    (rule subzero-program-atom-here
      (head
        (subzero-program-atom
          (subzero-program-cons ?occurrence ?atom ?rest)
          ?occurrence
          ?atom))
      (body))
    (rule subzero-program-atom-rest
      (head
        (subzero-program-atom
          (subzero-program-cons ?head-occurrence ?head-atom ?rest)
          ?occurrence
          ?atom))
      (body (subzero-program-atom ?rest ?occurrence ?atom)))

    ; Rules are not a privileged storage class.  They are ordinary program
    ; atoms for which this partial view succeeds.
    (rule subzero-rule-view-equality
      (head
        (subzero-rule-view ?program ?occurrence ?pattern ?template))
      (body
        (subzero-program-atom
          ?program
          ?occurrence
          (q-app
            (q-sym (q-str "="))
            (q-cons ?pattern (q-cons ?template q-nil))))))

    ; Strict ordering on quoted de Bruijn indices supports a canonical finite
    ; substitution map without negation-as-failure.
    (rule subzero-index-lt-base
      (head (subzero-index-lt q-zero (q-succ ?right)))
      (body))
    (rule subzero-index-lt-step
      (head
        (subzero-index-lt (q-succ ?left) (q-succ ?right)))
      (body (subzero-index-lt ?left ?right)))

    (rule subzero-bind-empty
      (head
        (subzero-bind
          ?index ?value subzero-env-nil
          (subzero-env-cons ?index ?value subzero-env-nil)))
      (body))
    (rule subzero-bind-existing
      (head
        (subzero-bind
          ?index ?value
          (subzero-env-cons ?index ?value ?rest)
          (subzero-env-cons ?index ?value ?rest)))
      (body))
    (rule subzero-bind-before
      (head
        (subzero-bind
          ?index ?value
          (subzero-env-cons ?next-index ?next-value ?rest)
          (subzero-env-cons
            ?index ?value
            (subzero-env-cons ?next-index ?next-value ?rest))))
      (body (subzero-index-lt ?index ?next-index)))
    (rule subzero-bind-after
      (head
        (subzero-bind
          ?index ?value
          (subzero-env-cons ?head-index ?head-value ?rest)
          (subzero-env-cons ?head-index ?head-value ?updated)))
      (body
        (subzero-index-lt ?head-index ?index)
        (subzero-bind ?index ?value ?rest ?updated)))

    (rule subzero-lookup-here
      (head
        (subzero-lookup
          ?index
          (subzero-env-cons ?index ?value ?rest)
          ?value))
      (body))
    (rule subzero-lookup-after
      (head
        (subzero-lookup
          ?index
          (subzero-env-cons ?head-index ?head-value ?rest)
          ?value))
      (body
        (subzero-index-lt ?head-index ?index)
        (subzero-lookup ?index ?rest ?value)))

    ; Matching is structural.  Repeated object variables must reuse exactly
    ; the binding already stored at their quoted index.
    (rule subzero-match-variable
      (head
        (subzero-match (q-var ?index) ?value ?before ?after))
      (body (subzero-bind ?index ?value ?before ?after)))
    (rule subzero-match-symbol
      (head
        (subzero-match (q-sym ?name) (q-sym ?name) ?env ?env))
      (body))
    (rule subzero-match-integer
      (head
        (subzero-match (q-int ?value) (q-int ?value) ?env ?env))
      (body))
    (rule subzero-match-string
      (head
        (subzero-match (q-str ?value) (q-str ?value) ?env ?env))
      (body))
    (rule subzero-match-empty-expression
      (head (subzero-match q-empty q-empty ?env ?env))
      (body))
    (rule subzero-match-ground
      (head
        (subzero-match
          (q-ground ?value) (q-ground ?value) ?env ?env))
      (body))
    (rule subzero-match-application
      (head
        (subzero-match
          (q-app ?pattern-head ?pattern-arguments)
          (q-app ?value-head ?value-arguments)
          ?before ?after))
      (body
        (subzero-match ?pattern-head ?value-head ?before ?middle)
        (subzero-match-list
          ?pattern-arguments ?value-arguments ?middle ?after)))
    (rule subzero-match-list-empty
      (head (subzero-match-list q-nil q-nil ?env ?env))
      (body))
    (rule subzero-match-list-step
      (head
        (subzero-match-list
          (q-cons ?pattern-head ?pattern-tail)
          (q-cons ?value-head ?value-tail)
          ?before ?after))
      (body
        (subzero-match ?pattern-head ?value-head ?before ?middle)
        (subzero-match-list
          ?pattern-tail ?value-tail ?middle ?after)))

    (rule subzero-substitute-variable
      (head (subzero-substitute (q-var ?index) ?env ?value))
      (body (subzero-lookup ?index ?env ?value)))
    (rule subzero-substitute-symbol
      (head
        (subzero-substitute (q-sym ?name) ?env (q-sym ?name)))
      (body))
    (rule subzero-substitute-integer
      (head
        (subzero-substitute (q-int ?value) ?env (q-int ?value)))
      (body))
    (rule subzero-substitute-string
      (head
        (subzero-substitute (q-str ?value) ?env (q-str ?value)))
      (body))
    (rule subzero-substitute-empty-expression
      (head (subzero-substitute q-empty ?env q-empty))
      (body))
    (rule subzero-substitute-ground
      (head
        (subzero-substitute
          (q-ground ?value) ?env (q-ground ?value)))
      (body))
    (rule subzero-substitute-application
      (head
        (subzero-substitute
          (q-app ?head ?arguments)
          ?env
          (q-app ?result-head ?result-arguments)))
      (body
        (subzero-substitute ?head ?env ?result-head)
        (subzero-substitute-list ?arguments ?env ?result-arguments)))
    (rule subzero-substitute-list-empty
      (head (subzero-substitute-list q-nil ?env q-nil))
      (body))
    (rule subzero-substitute-list-step
      (head
        (subzero-substitute-list
          (q-cons ?head ?tail)
          ?env
          (q-cons ?result-head ?result-tail)))
      (body
        (subzero-substitute ?head ?env ?result-head)
        (subzero-substitute-list ?tail ?env ?result-tail)))

    (rule subzero-root-step-from-rule-atom
      (head
        (subzero-root-step
          ?program ?subject
          (subzero-at-root ?occurrence ?environment)
          ?result))
      (body
        (subzero-rule-view ?program ?occurrence ?pattern ?template)
        (subzero-match
          ?pattern ?subject subzero-env-nil ?environment)
        (subzero-substitute ?template ?environment ?result)))

    ; Contextual closure chooses exactly one occurrence.  Root, expression
    ; head, and argument positions are distinguished by the occurrence proof.
    (rule subzero-step-root
      (head
        (subzero-step ?program ?subject ?occurrence ?result))
      (body
        (subzero-root-step ?program ?subject ?occurrence ?result)))
    (rule subzero-step-application-head
      (head
        (subzero-step
          ?program
          (q-app ?head ?arguments)
          (subzero-in-head ?occurrence)
          (q-app ?result-head ?arguments)))
      (body
        (subzero-step ?program ?head ?occurrence ?result-head)))
    (rule subzero-step-application-argument
      (head
        (subzero-step
          ?program
          (q-app ?head ?arguments)
          ?occurrence
          (q-app ?head ?result-arguments)))
      (body
        (subzero-step-list
          ?program ?arguments ?occurrence ?result-arguments)))
    (rule subzero-step-list-head
      (head
        (subzero-step-list
          ?program
          (q-cons ?head ?tail)
          (subzero-in-argument q-zero ?occurrence)
          (q-cons ?result-head ?tail)))
      (body
        (subzero-step ?program ?head ?occurrence ?result-head)))
    (rule subzero-step-list-tail
      (head
        (subzero-step-list
          ?program
          (q-cons ?head ?tail)
          (subzero-in-argument (q-succ ?index) ?occurrence)
          (q-cons ?head ?result-tail)))
      (body
        (subzero-step-list
          ?program
          ?tail
          (subzero-in-argument ?index ?occurrence)
          ?result-tail)))
  ))
