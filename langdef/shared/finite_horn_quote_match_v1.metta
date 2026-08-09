; Structural matching and substitution for the finite-Horn quotation ABI.
; Object variables are quoted q-var indices, distinct from variables of this
; presentation.  The ordered environment makes repeated object variables
; agree without relying on negation or host interning.
(gslt-presentation-v1 FiniteHornQuoteMatchV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-ground 1)
    (operator q-int 1)
    (operator q-str 1)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)
    (operator qmatch-bind 4)
    (operator qmatch-env-cons 3)
    (operator qmatch-index-lt 2)
    (operator qmatch-lookup 3)
    (operator qmatch-term 4)
    (operator qmatch-term-list 4)
    (operator qmatch-substitute 3)
    (operator qmatch-substitute-list 3))
  (equations)
  (rewrites
    (rule qmatch-index-lt-base
      (head (qmatch-index-lt q-zero (q-succ ?right)))
      (body))
    (rule qmatch-index-lt-step
      (head (qmatch-index-lt (q-succ ?left) (q-succ ?right)))
      (body (qmatch-index-lt ?left ?right)))

    (rule qmatch-bind-empty
      (head
        (qmatch-bind
          ?index ?value qmatch-env-nil
          (qmatch-env-cons ?index ?value qmatch-env-nil)))
      (body))
    (rule qmatch-bind-existing
      (head
        (qmatch-bind
          ?index ?value
          (qmatch-env-cons ?index ?value ?rest)
          (qmatch-env-cons ?index ?value ?rest)))
      (body))
    (rule qmatch-bind-before
      (head
        (qmatch-bind
          ?index ?value
          (qmatch-env-cons ?next-index ?next-value ?rest)
          (qmatch-env-cons
            ?index ?value
            (qmatch-env-cons ?next-index ?next-value ?rest))))
      (body (qmatch-index-lt ?index ?next-index)))
    (rule qmatch-bind-after
      (head
        (qmatch-bind
          ?index ?value
          (qmatch-env-cons ?head-index ?head-value ?rest)
          (qmatch-env-cons ?head-index ?head-value ?updated)))
      (body
        (qmatch-index-lt ?head-index ?index)
        (qmatch-bind ?index ?value ?rest ?updated)))

    (rule qmatch-lookup-here
      (head
        (qmatch-lookup
          ?index
          (qmatch-env-cons ?index ?value ?rest)
          ?value))
      (body))
    (rule qmatch-lookup-after
      (head
        (qmatch-lookup
          ?index
          (qmatch-env-cons ?head-index ?head-value ?rest)
          ?value))
      (body
        (qmatch-index-lt ?head-index ?index)
        (qmatch-lookup ?index ?rest ?value)))

    (rule qmatch-term-variable
      (head (qmatch-term (q-var ?index) ?value ?before ?after))
      (body (qmatch-bind ?index ?value ?before ?after)))
    (rule qmatch-term-symbol
      (head (qmatch-term (q-sym ?name) (q-sym ?name) ?env ?env))
      (body))
    (rule qmatch-term-integer
      (head (qmatch-term (q-int ?value) (q-int ?value) ?env ?env))
      (body))
    (rule qmatch-term-string
      (head (qmatch-term (q-str ?value) (q-str ?value) ?env ?env))
      (body))
    (rule qmatch-term-empty-expression
      (head (qmatch-term q-empty q-empty ?env ?env))
      (body))
    (rule qmatch-term-ground
      (head
        (qmatch-term (q-ground ?value) (q-ground ?value) ?env ?env))
      (body))
    (rule qmatch-term-application
      (head
        (qmatch-term
          (q-app ?pattern-head ?pattern-arguments)
          (q-app ?value-head ?value-arguments)
          ?before ?after))
      (body
        (qmatch-term ?pattern-head ?value-head ?before ?middle)
        (qmatch-term-list
          ?pattern-arguments ?value-arguments ?middle ?after)))
    (rule qmatch-term-list-empty
      (head (qmatch-term-list q-nil q-nil ?env ?env))
      (body))
    (rule qmatch-term-list-step
      (head
        (qmatch-term-list
          (q-cons ?pattern-head ?pattern-tail)
          (q-cons ?value-head ?value-tail)
          ?before ?after))
      (body
        (qmatch-term ?pattern-head ?value-head ?before ?middle)
        (qmatch-term-list
          ?pattern-tail ?value-tail ?middle ?after)))

    (rule qmatch-substitute-variable
      (head (qmatch-substitute (q-var ?index) ?env ?value))
      (body (qmatch-lookup ?index ?env ?value)))
    (rule qmatch-substitute-symbol
      (head (qmatch-substitute (q-sym ?name) ?env (q-sym ?name)))
      (body))
    (rule qmatch-substitute-integer
      (head (qmatch-substitute (q-int ?value) ?env (q-int ?value)))
      (body))
    (rule qmatch-substitute-string
      (head (qmatch-substitute (q-str ?value) ?env (q-str ?value)))
      (body))
    (rule qmatch-substitute-empty-expression
      (head (qmatch-substitute q-empty ?env q-empty))
      (body))
    (rule qmatch-substitute-ground
      (head
        (qmatch-substitute (q-ground ?value) ?env (q-ground ?value)))
      (body))
    (rule qmatch-substitute-application
      (head
        (qmatch-substitute
          (q-app ?head ?arguments)
          ?env
          (q-app ?result-head ?result-arguments)))
      (body
        (qmatch-substitute ?head ?env ?result-head)
        (qmatch-substitute-list ?arguments ?env ?result-arguments)))
    (rule qmatch-substitute-list-empty
      (head (qmatch-substitute-list q-nil ?env q-nil))
      (body))
    (rule qmatch-substitute-list-step
      (head
        (qmatch-substitute-list
          (q-cons ?head ?tail)
          ?env
          (q-cons ?result-head ?result-tail)))
      (body
        (qmatch-substitute ?head ?env ?result-head)
        (qmatch-substitute-list ?tail ?env ?result-tail)))
  ))
