; Capture-avoiding substitution for an open continuation.  Bindings supplied
; by the current match are replaced; variables belonging to nested later
; computations remain open data.  The numeric q-var index is an
; occurrence-local host matching slot; canonical object binders are explicit
; (idx n) data and require the separate support-indexed ABT discipline.
(gslt-presentation-v1 MettaZeroOpenSubstitutionV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1)
    (operator q-var 1)
    (operator qmatch-env-cons 3)
    (operator qmatch-index-lt 2)
    (operator qmatch-open-variable 3)
    (operator qmatch-substitute-open 3)
    (operator qmatch-substitute-open-list 3))
  (equations)
  (rewrites
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
  ))
