; Support-indexed matching and open substitution for canonical ABTs.  Match
; variables remain q-var slots; object binders remain ordinary quoted (idx n)
; terms whose field depths come from the admitted ABT signature provider.
(gslt-presentation-v1 MettaZeroSupportIndexedABTMatchV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-ground 1)
    (operator q-int 1)
    (operator q-str 1)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)

    ; Generic provider leaves.
    (operator qabt-field-depth 4)
    (operator qabt-transport 4)

    ; Authored depth arithmetic and occurrence-aware environments.
    (operator qabt-nat-add 3)
    (operator qmatch-supported-bind 6)
    (operator qmatch-supported-binding 3)
    (operator qmatch-supported-bound 3)
    (operator qmatch-supported-env-cons 4)
    (operator qmatch-supported-open-variable 5)
    (operator qmatch-supported-term 5)
    (operator qmatch-supported-term-list 9)
    (operator qmatch-supported-unbound 1)
    (operator qmatch-substitute-supported 4)
    (operator qmatch-substitute-supported-list 7))
  (equations)
  (rewrites
    (rule qabt-nat-add-zero
      (head (qabt-nat-add q-zero ?right ?right))
      (body))
    (rule qabt-nat-add-succ
      (head
        (qabt-nat-add
          (q-succ ?left) ?right (q-succ ?result)))
      (body (qabt-nat-add ?left ?right ?result)))

    (rule qmatch-supported-bind-empty
      (head
        (qmatch-supported-bind
          ?index ?depth ?value qmatch-supported-env-nil
          (qmatch-supported-env-cons
            ?index ?depth ?value qmatch-supported-env-nil)
          (qmatch-supported-binding ?index ?depth ?value)))
      (body))
    (rule qmatch-supported-bind-existing
      (head
        (qmatch-supported-bind
          ?index ?depth ?value
          (qmatch-supported-env-cons
            ?index ?support ?base ?rest)
          (qmatch-supported-env-cons
            ?index ?support ?base ?rest)
          (qmatch-supported-binding ?index ?support ?base)))
      (body
        (qabt-transport ?support ?depth ?base ?value)))
    (rule qmatch-supported-bind-before
      (head
        (qmatch-supported-bind
          ?index ?depth ?value
          (qmatch-supported-env-cons
            ?next-index ?next-support ?next-value ?rest)
          (qmatch-supported-env-cons
            ?index ?depth ?value
            (qmatch-supported-env-cons
              ?next-index ?next-support ?next-value ?rest))
          (qmatch-supported-binding ?index ?depth ?value)))
      (body (qmatch-index-lt ?index ?next-index)))
    (rule qmatch-supported-bind-after
      (head
        (qmatch-supported-bind
          ?index ?depth ?value
          (qmatch-supported-env-cons
            ?head-index ?head-support ?head-value ?rest)
          (qmatch-supported-env-cons
            ?head-index ?head-support ?head-value ?updated)
          ?binding))
      (body
        (qmatch-index-lt ?head-index ?index)
        (qmatch-supported-bind
          ?index ?depth ?value ?rest ?updated ?binding)))

    (rule qmatch-supported-open-variable-empty
      (head
        (qmatch-supported-open-variable
          ?index ?depth qmatch-supported-env-nil
          (q-var ?index) (qmatch-supported-unbound ?index)))
      (body))
    (rule qmatch-supported-open-variable-here
      (head
        (qmatch-supported-open-variable
          ?index ?depth
          (qmatch-supported-env-cons ?index ?support ?base ?rest)
          ?value (qmatch-supported-bound ?index ?support ?base)))
      (body
        (qabt-transport ?support ?depth ?base ?value)))
    (rule qmatch-supported-open-variable-before
      (head
        (qmatch-supported-open-variable
          ?index ?depth
          (qmatch-supported-env-cons
            ?next-index ?next-support ?next-value ?rest)
          (q-var ?index) (qmatch-supported-unbound ?index)))
      (body (qmatch-index-lt ?index ?next-index)))
    (rule qmatch-supported-open-variable-after
      (head
        (qmatch-supported-open-variable
          ?index ?depth
          (qmatch-supported-env-cons
            ?head-index ?head-support ?head-value ?rest)
          ?value ?evidence))
      (body
        (qmatch-index-lt ?head-index ?index)
        (qmatch-supported-open-variable
          ?index ?depth ?rest ?value ?evidence)))

    (rule qmatch-supported-term-variable
      (head
        (qmatch-supported-term
          (q-var ?index) ?value ?depth ?before ?after))
      (body
        (qmatch-supported-bind
          ?index ?depth ?value ?before ?after ?binding)))
    (rule qmatch-supported-term-symbol
      (head
        (qmatch-supported-term
          (q-sym ?name) (q-sym ?name) ?depth ?env ?env))
      (body))
    (rule qmatch-supported-term-integer
      (head
        (qmatch-supported-term
          (q-int ?value) (q-int ?value) ?depth ?env ?env))
      (body))
    (rule qmatch-supported-term-string
      (head
        (qmatch-supported-term
          (q-str ?value) (q-str ?value) ?depth ?env ?env))
      (body))
    (rule qmatch-supported-term-empty-expression
      (head
        (qmatch-supported-term q-empty q-empty ?depth ?env ?env))
      (body))
    (rule qmatch-supported-term-ground
      (head
        (qmatch-supported-term
          (q-ground ?value) (q-ground ?value) ?depth ?env ?env))
      (body))
    (rule qmatch-supported-term-application
      (head
        (qmatch-supported-term
          (q-app ?pattern-head ?pattern-arguments)
          (q-app ?value-head ?value-arguments)
          ?depth ?before ?after))
      (body
        (qmatch-supported-term
          ?pattern-head ?value-head ?depth ?before ?middle)
        (qmatch-supported-term-list
          ?value-head ?value-arguments q-zero ?depth
          ?pattern-arguments ?value-arguments ?middle ?after
          qmatch-supported-list-evidence)))
    (rule qmatch-supported-term-list-empty
      (head
        (qmatch-supported-term-list
          ?constructor ?arguments ?position ?depth
          q-nil q-nil ?env ?env qmatch-supported-list-evidence))
      (body))
    (rule qmatch-supported-term-list-step
      (head
        (qmatch-supported-term-list
          ?constructor ?arguments ?position ?depth
          (q-cons ?pattern-head ?pattern-tail)
          (q-cons ?value-head ?value-tail)
          ?before ?after qmatch-supported-list-evidence))
      (body
        (qabt-field-depth
          ?constructor ?arguments ?position ?field-depth)
        (qabt-nat-add ?depth ?field-depth ?field-use-depth)
        (qmatch-supported-term
          ?pattern-head ?value-head ?field-use-depth ?before ?middle)
        (qmatch-supported-term-list
          ?constructor ?arguments (q-succ ?position) ?depth
          ?pattern-tail ?value-tail ?middle ?after
          qmatch-supported-list-evidence)))

    (rule qmatch-substitute-supported-variable
      (head
        (qmatch-substitute-supported
          (q-var ?index) ?environment ?depth ?result))
      (body
        (qmatch-supported-open-variable
          ?index ?depth ?environment ?result ?evidence)))
    (rule qmatch-substitute-supported-symbol
      (head
        (qmatch-substitute-supported
          (q-sym ?name) ?environment ?depth (q-sym ?name)))
      (body))
    (rule qmatch-substitute-supported-integer
      (head
        (qmatch-substitute-supported
          (q-int ?value) ?environment ?depth (q-int ?value)))
      (body))
    (rule qmatch-substitute-supported-string
      (head
        (qmatch-substitute-supported
          (q-str ?value) ?environment ?depth (q-str ?value)))
      (body))
    (rule qmatch-substitute-supported-empty-expression
      (head
        (qmatch-substitute-supported
          q-empty ?environment ?depth q-empty))
      (body))
    (rule qmatch-substitute-supported-ground
      (head
        (qmatch-substitute-supported
          (q-ground ?value) ?environment ?depth (q-ground ?value)))
      (body))
    (rule qmatch-substitute-supported-application
      (head
        (qmatch-substitute-supported
          (q-app ?head ?arguments) ?environment ?depth
          (q-app ?result-head ?result-arguments)))
      (body
        (qmatch-substitute-supported
          ?head ?environment ?depth ?result-head)
        (qmatch-substitute-supported-list
          ?head ?arguments q-zero ?depth
          ?arguments ?environment ?result-arguments)))
    (rule qmatch-substitute-supported-list-empty
      (head
        (qmatch-substitute-supported-list
          ?constructor ?arguments ?position ?depth
          q-nil ?environment q-nil))
      (body))
    (rule qmatch-substitute-supported-list-step
      (head
        (qmatch-substitute-supported-list
          ?constructor ?arguments ?position ?depth
          (q-cons ?head ?tail) ?environment
          (q-cons ?result-head ?result-tail)))
      (body
        (qabt-field-depth
          ?constructor ?arguments ?position ?field-depth)
        (qabt-nat-add ?depth ?field-depth ?field-use-depth)
        (qmatch-substitute-supported
          ?head ?environment ?field-use-depth ?result-head)
        (qmatch-substitute-supported-list
          ?constructor ?arguments (q-succ ?position) ?depth
          ?tail ?environment ?result-tail)))
  ))
