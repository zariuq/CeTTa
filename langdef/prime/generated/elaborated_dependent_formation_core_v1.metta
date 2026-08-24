; Authored finite-Horn presentation of Prime's post-name-resolution dependent
; formation core.  `PrimeTeleBind` represents one already-resolved telescope
; binder; bound occurrences use structural Peano indices.  The native Prime
; elaborator separately lowers named syntax binders to integer `idx` atoms.
; `PrimeCanonicalForm` is therefore an internal canonical judgment, not a
; claim that canonical `Pi`/`idx` syntax is accepted as a syntax `Form`.
;
; This is deliberately a fragment.  It covers primitive and declared type
; names, scoped indices, Pi formation, and telescope lowering.  It does not
; claim universes, term synthesis, lambda/application typing, refinements, or
; full source/native adequacy.
(gslt-presentation-v1 prime-elaborated-dependent-formation-core-v1
  (signature
    (operator PrimeNamed 1)
    (operator PrimeSigCons 2)
    (operator PrimeCtxCons 2)
    (operator PrimeSucc 1)
    (operator idx 1)
    (operator Pi 2)
    (operator PrimeTeleDone 1)
    (operator PrimeTeleBind 2)
    (operator PrimeDeclared 2)
    (operator PrimeIndexIn 2)
    (operator PrimeContextValid 2)
    (operator PrimeCanonicalForm 3)
    (operator PrimeElaborates 4)
    (operator PrimeClosedElaborates 3)
  )
  (equations)
  (rewrites
    (rule prime-dependent-declared-head
      (head (PrimeDeclared ?type (PrimeSigCons ?type ?rest)))
      (body))
    (rule prime-dependent-declared-tail
      (head (PrimeDeclared ?type (PrimeSigCons ?other ?rest)))
      (body (PrimeDeclared ?type ?rest)))

    (rule prime-dependent-index-zero
      (head (PrimeIndexIn PrimeZero (PrimeCtxCons ?domain ?context)))
      (body))
    (rule prime-dependent-index-successor
      (head
        (PrimeIndexIn (PrimeSucc ?index)
          (PrimeCtxCons ?domain ?context)))
      (body (PrimeIndexIn ?index ?context)))

    (rule prime-dependent-context-empty
      (head (PrimeContextValid ?signature PrimeCtxNil))
      (body))
    (rule prime-dependent-context-bind
      (head
        (PrimeContextValid ?signature
          (PrimeCtxCons ?domain ?context)))
      (body
        (PrimeContextValid ?signature ?context)
        (PrimeCanonicalForm ?signature ?context ?domain)))

    (rule prime-dependent-form-type
      (head (PrimeCanonicalForm ?signature ?context Type))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-dynamic
      (head (PrimeCanonicalForm ?signature ?context PDynamic))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-atom
      (head (PrimeCanonicalForm ?signature ?context Atom))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-symbol
      (head (PrimeCanonicalForm ?signature ?context Symbol))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-variable
      (head (PrimeCanonicalForm ?signature ?context Variable))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-expression
      (head (PrimeCanonicalForm ?signature ?context Expression))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-grounded
      (head (PrimeCanonicalForm ?signature ?context Grounded))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-number
      (head (PrimeCanonicalForm ?signature ?context Number))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-bool
      (head (PrimeCanonicalForm ?signature ?context Bool))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-string
      (head (PrimeCanonicalForm ?signature ?context String))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-error
      (head (PrimeCanonicalForm ?signature ?context ErrorType))
      (body (PrimeContextValid ?signature ?context)))
    (rule prime-dependent-form-declared
      (head
        (PrimeCanonicalForm ?signature ?context (PrimeNamed ?name)))
      (body
        (PrimeContextValid ?signature ?context)
        (PrimeDeclared (PrimeNamed ?name) ?signature)))
    (rule prime-dependent-form-index
      (head
        (PrimeCanonicalForm ?signature ?context (idx ?index)))
      (body
        (PrimeContextValid ?signature ?context)
        (PrimeIndexIn ?index ?context)))
    (rule prime-dependent-form-pi
      (head
        (PrimeCanonicalForm ?signature ?context (Pi ?domain ?body)))
      (body
        (PrimeCanonicalForm ?signature ?context ?domain)
        (PrimeCanonicalForm ?signature
          (PrimeCtxCons ?domain ?context) ?body)))

    (rule prime-dependent-elaborate-done
      (head
        (PrimeElaborates ?signature ?context
          (PrimeTeleDone ?result) ?result))
      (body (PrimeCanonicalForm ?signature ?context ?result)))
    (rule prime-dependent-elaborate-bind
      (head
        (PrimeElaborates ?signature ?context
          (PrimeTeleBind ?domain ?rest) (Pi ?domain ?body)))
      (body
        (PrimeCanonicalForm ?signature ?context ?domain)
        (PrimeElaborates ?signature
          (PrimeCtxCons ?domain ?context) ?rest ?body)))
    (rule prime-dependent-elaborate-closed
      (head
        (PrimeClosedElaborates ?signature ?telescope ?canonical))
      (body
        (PrimeElaborates ?signature PrimeCtxNil
          ?telescope ?canonical)))
  ))
