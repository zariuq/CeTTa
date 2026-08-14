; Authored finite-Horn presentation of Prime's stable open lambda-Pi fragment.
; Context entries and bound occurrences use structural Peano indices here;
; the native direct judge uses integer `(idx n)` atoms for the same ABT.
; The public judgments keep the existing PSynth/PCheck/PConvert arities and
; carry the open context inside `PrimeScoped`.
(gslt-presentation-v1 prime-open-lambda-pi-core-v1
  (nik-authority-frame-v1
    (mode direct-decision)
    (certificate-policy none)
    (fiber prime)
    (outcome-algebra (PEstablished PRefuted PUndetermined PIncomplete)
      (exclusive)
      (default PUndetermined))
    (native-projection pending)
    (status AUTHORED_FRAGMENT)
    (commitments direct-computation certificate-free exclusive-outcomes explicit-default open-context beta-eta-conversion synthesis-formed-by-construction))
  (signature
    (operator PrimeScoped 2)
    (operator PrimeCtxCons 2)
    (operator PrimeSucc 1)
    (operator idx 1)
    (operator Pi 2)
    (operator Lam 2)
    (operator App 2)
    (operator PSynth 2)
    (operator PCheck 2)
    (operator PConvert 2)
    (operator PrimeLpJudges 2)
    (operator PrimeLpNatLt 2)
    (operator PrimeLpNatLe 2)
    (operator PrimeLpNatNe 2)
    (operator PrimeLpNatAdd 3)
    (operator PrimeLpRemoveAfter 3)
    (operator PrimeLpShift 4)
    (operator PrimeLpLower 3)
    (operator PrimeLpUsesAt 2)
    (operator PrimeLpUnusedAt 2)
    (operator PrimeLpNotIndexZero 1)
    (operator PrimeLpEtaStableBody 1)
    (operator PrimeLpSubstitute 4)
    (operator PrimeLpNormalizes 2)
    (operator PrimeLpNeutral 1)
    (operator PrimeLpConvertible 2)
    (operator PrimeLpLookup 3)
    (operator PrimeLpContextValid 1)
    (operator PrimeLpOrdinaryType 2)
    (operator PrimeLpSynthCore 3)
    (operator PrimeLpCheckCore 3)
    (operator PrimeLpNotLam 1)
  )
  (equations)
  (rewrites
    (rule prime-lp-nat-lt-zero
      (nik-rule-frame-v1
        (native-projection native-structural-index-order)
        (role calculus))
      (head (PrimeLpNatLt PrimeZero (PrimeSucc ?right)))
      (body))
    (rule prime-lp-nat-lt-successor
      (nik-rule-frame-v1
        (native-projection native-structural-index-order)
        (role calculus))
      (head
        (PrimeLpNatLt (PrimeSucc ?left) (PrimeSucc ?right)))
      (body (PrimeLpNatLt ?left ?right)))
    (rule prime-lp-nat-le-zero
      (nik-rule-frame-v1
        (native-projection native-structural-index-order)
        (role calculus))
      (head (PrimeLpNatLe PrimeZero ?right))
      (body))
    (rule prime-lp-nat-le-successor
      (nik-rule-frame-v1
        (native-projection native-structural-index-order)
        (role calculus))
      (head
        (PrimeLpNatLe (PrimeSucc ?left) (PrimeSucc ?right)))
      (body (PrimeLpNatLe ?left ?right)))
    (rule prime-lp-nat-ne-zero-successor
      (nik-rule-frame-v1
        (native-projection native-structural-index-inequality)
        (role calculus))
      (head (PrimeLpNatNe PrimeZero (PrimeSucc ?right)))
      (body))
    (rule prime-lp-nat-ne-successor-zero
      (nik-rule-frame-v1
        (native-projection native-structural-index-inequality)
        (role calculus))
      (head (PrimeLpNatNe (PrimeSucc ?left) PrimeZero))
      (body))
    (rule prime-lp-nat-ne-successor
      (nik-rule-frame-v1
        (native-projection native-structural-index-inequality)
        (role calculus))
      (head
        (PrimeLpNatNe (PrimeSucc ?left) (PrimeSucc ?right)))
      (body (PrimeLpNatNe ?left ?right)))
    (rule prime-lp-nat-add-zero
      (nik-rule-frame-v1
        (native-projection native-structural-index-addition)
        (role calculus))
      (head (PrimeLpNatAdd PrimeZero ?right ?right))
      (body))
    (rule prime-lp-nat-add-successor
      (nik-rule-frame-v1
        (native-projection native-structural-index-addition)
        (role calculus))
      (head
        (PrimeLpNatAdd (PrimeSucc ?left) ?right (PrimeSucc ?sum)))
      (body (PrimeLpNatAdd ?left ?right ?sum)))
    (rule prime-lp-remove-after-zero
      (nik-rule-frame-v1
        (native-projection native-structural-index-predecessor)
        (role calculus))
      (head
        (PrimeLpRemoveAfter PrimeZero (PrimeSucc ?index) ?index))
      (body))
    (rule prime-lp-remove-after-successor
      (nik-rule-frame-v1
        (native-projection native-structural-index-predecessor)
        (role calculus))
      (head
        (PrimeLpRemoveAfter (PrimeSucc ?depth) (PrimeSucc ?index)
          (PrimeSucc ?result)))
      (body (PrimeLpRemoveAfter ?depth ?index ?result)))

    (rule prime-lp-shift-u0
      (nik-rule-frame-v1
        (native-projection native-debruijn-shift)
        (role calculus))
      (head (PrimeLpShift ?amount ?cutoff U0 U0))
      (body))
    (rule prime-lp-shift-u1
      (nik-rule-frame-v1
        (native-projection native-debruijn-shift)
        (role calculus))
      (head (PrimeLpShift ?amount ?cutoff U1 U1))
      (body))
    (rule prime-lp-shift-index-below
      (nik-rule-frame-v1
        (native-projection native-debruijn-shift)
        (role calculus))
      (head
        (PrimeLpShift ?amount ?cutoff (idx ?index) (idx ?index)))
      (body (PrimeLpNatLt ?index ?cutoff)))
    (rule prime-lp-shift-index-at-or-above
      (nik-rule-frame-v1
        (native-projection native-debruijn-shift)
        (role calculus))
      (head
        (PrimeLpShift ?amount ?cutoff (idx ?index) (idx ?shifted)))
      (body
        (PrimeLpNatLe ?cutoff ?index)
        (PrimeLpNatAdd ?amount ?index ?shifted)))
    (rule prime-lp-shift-pi
      (nik-rule-frame-v1
        (native-projection native-debruijn-shift)
        (role calculus))
      (head
        (PrimeLpShift ?amount ?cutoff (Pi ?domain ?body)
          (Pi ?shifted-domain ?shifted-body)))
      (body
        (PrimeLpShift ?amount ?cutoff ?domain ?shifted-domain)
        (PrimeLpShift ?amount (PrimeSucc ?cutoff)
          ?body ?shifted-body)))
    (rule prime-lp-shift-lam
      (nik-rule-frame-v1
        (native-projection native-debruijn-shift)
        (role calculus))
      (head
        (PrimeLpShift ?amount ?cutoff (Lam ?domain ?body)
          (Lam ?shifted-domain ?shifted-body)))
      (body
        (PrimeLpShift ?amount ?cutoff ?domain ?shifted-domain)
        (PrimeLpShift ?amount (PrimeSucc ?cutoff)
          ?body ?shifted-body)))
    (rule prime-lp-shift-app
      (nik-rule-frame-v1
        (native-projection native-debruijn-shift)
        (role calculus))
      (head
        (PrimeLpShift ?amount ?cutoff (App ?function ?argument)
          (App ?shifted-function ?shifted-argument)))
      (body
        (PrimeLpShift ?amount ?cutoff ?function ?shifted-function)
        (PrimeLpShift ?amount ?cutoff ?argument ?shifted-argument)))

    (rule prime-lp-lower-u0
      (nik-rule-frame-v1
        (native-projection native-debruijn-lower)
        (role calculus))
      (head (PrimeLpLower ?cutoff U0 U0))
      (body))
    (rule prime-lp-lower-u1
      (nik-rule-frame-v1
        (native-projection native-debruijn-lower)
        (role calculus))
      (head (PrimeLpLower ?cutoff U1 U1))
      (body))
    (rule prime-lp-lower-index-below
      (nik-rule-frame-v1
        (native-projection native-debruijn-lower)
        (role calculus))
      (head (PrimeLpLower ?cutoff (idx ?index) (idx ?index)))
      (body (PrimeLpNatLt ?index ?cutoff)))
    (rule prime-lp-lower-index-at-or-above
      (nik-rule-frame-v1
        (native-projection native-debruijn-lower)
        (role calculus))
      (head
        (PrimeLpLower ?cutoff (idx (PrimeSucc ?index)) (idx ?index)))
      (body (PrimeLpNatLe ?cutoff (PrimeSucc ?index))))
    (rule prime-lp-lower-pi
      (nik-rule-frame-v1
        (native-projection native-debruijn-lower)
        (role calculus))
      (head
        (PrimeLpLower ?cutoff (Pi ?domain ?body)
          (Pi ?lowered-domain ?lowered-body)))
      (body
        (PrimeLpLower ?cutoff ?domain ?lowered-domain)
        (PrimeLpLower (PrimeSucc ?cutoff) ?body ?lowered-body)))
    (rule prime-lp-lower-lam
      (nik-rule-frame-v1
        (native-projection native-debruijn-lower)
        (role calculus))
      (head
        (PrimeLpLower ?cutoff (Lam ?domain ?body)
          (Lam ?lowered-domain ?lowered-body)))
      (body
        (PrimeLpLower ?cutoff ?domain ?lowered-domain)
        (PrimeLpLower (PrimeSucc ?cutoff) ?body ?lowered-body)))
    (rule prime-lp-lower-app
      (nik-rule-frame-v1
        (native-projection native-debruijn-lower)
        (role calculus))
      (head
        (PrimeLpLower ?cutoff (App ?function ?argument)
          (App ?lowered-function ?lowered-argument)))
      (body
        (PrimeLpLower ?cutoff ?function ?lowered-function)
        (PrimeLpLower ?cutoff ?argument ?lowered-argument)))

    (rule prime-lp-uses-index
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUsesAt ?index (idx ?index)))
      (body))
    (rule prime-lp-uses-pi-domain
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUsesAt ?index (Pi ?domain ?body)))
      (body (PrimeLpUsesAt ?index ?domain)))
    (rule prime-lp-uses-pi-body
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUsesAt ?index (Pi ?domain ?body)))
      (body (PrimeLpUsesAt (PrimeSucc ?index) ?body)))
    (rule prime-lp-uses-lam-domain
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUsesAt ?index (Lam ?domain ?body)))
      (body (PrimeLpUsesAt ?index ?domain)))
    (rule prime-lp-uses-lam-body
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUsesAt ?index (Lam ?domain ?body)))
      (body (PrimeLpUsesAt (PrimeSucc ?index) ?body)))
    (rule prime-lp-uses-app-function
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUsesAt ?index (App ?function ?argument)))
      (body (PrimeLpUsesAt ?index ?function)))
    (rule prime-lp-uses-app-argument
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUsesAt ?index (App ?function ?argument)))
      (body (PrimeLpUsesAt ?index ?argument)))
    (rule prime-lp-unused-u0
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUnusedAt ?index U0))
      (body))
    (rule prime-lp-unused-u1
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUnusedAt ?index U1))
      (body))
    (rule prime-lp-unused-index
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUnusedAt ?index (idx ?other)))
      (body (PrimeLpNatNe ?index ?other)))
    (rule prime-lp-unused-pi
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUnusedAt ?index (Pi ?domain ?body)))
      (body
        (PrimeLpUnusedAt ?index ?domain)
        (PrimeLpUnusedAt (PrimeSucc ?index) ?body)))
    (rule prime-lp-unused-lam
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUnusedAt ?index (Lam ?domain ?body)))
      (body
        (PrimeLpUnusedAt ?index ?domain)
        (PrimeLpUnusedAt (PrimeSucc ?index) ?body)))
    (rule prime-lp-unused-app
      (nik-rule-frame-v1
        (native-projection native-free-index-occurrence)
        (role calculus))
      (head (PrimeLpUnusedAt ?index (App ?function ?argument)))
      (body
        (PrimeLpUnusedAt ?index ?function)
        (PrimeLpUnusedAt ?index ?argument)))

    (rule prime-lp-not-index-zero-u0
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpNotIndexZero U0))
      (body))
    (rule prime-lp-not-index-zero-u1
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpNotIndexZero U1))
      (body))
    (rule prime-lp-not-index-zero-successor
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpNotIndexZero (idx (PrimeSucc ?index))))
      (body))
    (rule prime-lp-not-index-zero-pi
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpNotIndexZero (Pi ?domain ?body)))
      (body))
    (rule prime-lp-not-index-zero-lam
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpNotIndexZero (Lam ?domain ?body)))
      (body))
    (rule prime-lp-not-index-zero-app
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpNotIndexZero (App ?function ?argument)))
      (body))
    (rule prime-lp-eta-stable-u0
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpEtaStableBody U0))
      (body))
    (rule prime-lp-eta-stable-u1
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpEtaStableBody U1))
      (body))
    (rule prime-lp-eta-stable-index
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpEtaStableBody (idx ?index)))
      (body))
    (rule prime-lp-eta-stable-pi
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpEtaStableBody (Pi ?domain ?body)))
      (body))
    (rule prime-lp-eta-stable-lam
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpEtaStableBody (Lam ?domain ?body)))
      (body))
    (rule prime-lp-eta-stable-app-other-argument
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head (PrimeLpEtaStableBody (App ?function ?argument)))
      (body (PrimeLpNotIndexZero ?argument)))
    (rule prime-lp-eta-stable-app-dependent-function
      (nik-rule-frame-v1
        (native-projection native-eta-shape)
        (role calculus))
      (head
        (PrimeLpEtaStableBody
          (App ?function (idx PrimeZero))))
      (body (PrimeLpUsesAt PrimeZero ?function)))

    (rule prime-lp-substitute-u0
      (nik-rule-frame-v1
        (native-projection native-debruijn-substitution)
        (role calculus))
      (head (PrimeLpSubstitute ?argument ?depth U0 U0))
      (body))
    (rule prime-lp-substitute-u1
      (nik-rule-frame-v1
        (native-projection native-debruijn-substitution)
        (role calculus))
      (head (PrimeLpSubstitute ?argument ?depth U1 U1))
      (body))
    (rule prime-lp-substitute-index-below
      (nik-rule-frame-v1
        (native-projection native-debruijn-substitution)
        (role calculus))
      (head
        (PrimeLpSubstitute ?argument ?depth (idx ?index) (idx ?index)))
      (body (PrimeLpNatLt ?index ?depth)))
    (rule prime-lp-substitute-index-equal
      (nik-rule-frame-v1
        (native-projection native-debruijn-substitution)
        (role calculus))
      (head
        (PrimeLpSubstitute ?argument ?depth (idx ?depth) ?lifted))
      (body (PrimeLpShift ?depth PrimeZero ?argument ?lifted)))
    (rule prime-lp-substitute-index-above
      (nik-rule-frame-v1
        (native-projection native-debruijn-substitution)
        (role calculus))
      (head
        (PrimeLpSubstitute ?argument ?depth (idx ?index) (idx ?result)))
      (body (PrimeLpRemoveAfter ?depth ?index ?result)))
    (rule prime-lp-substitute-pi
      (nik-rule-frame-v1
        (native-projection native-debruijn-substitution)
        (role calculus))
      (head
        (PrimeLpSubstitute ?argument ?depth (Pi ?domain ?body)
          (Pi ?result-domain ?result-body)))
      (body
        (PrimeLpSubstitute ?argument ?depth ?domain ?result-domain)
        (PrimeLpSubstitute ?argument (PrimeSucc ?depth)
          ?body ?result-body)))
    (rule prime-lp-substitute-lam
      (nik-rule-frame-v1
        (native-projection native-debruijn-substitution)
        (role calculus))
      (head
        (PrimeLpSubstitute ?argument ?depth (Lam ?domain ?body)
          (Lam ?result-domain ?result-body)))
      (body
        (PrimeLpSubstitute ?argument ?depth ?domain ?result-domain)
        (PrimeLpSubstitute ?argument (PrimeSucc ?depth)
          ?body ?result-body)))
    (rule prime-lp-substitute-app
      (nik-rule-frame-v1
        (native-projection native-debruijn-substitution)
        (role calculus))
      (head
        (PrimeLpSubstitute ?argument ?depth (App ?function ?value)
          (App ?result-function ?result-value)))
      (body
        (PrimeLpSubstitute ?argument ?depth ?function ?result-function)
        (PrimeLpSubstitute ?argument ?depth ?value ?result-value)))

    (rule prime-lp-neutral-index
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head (PrimeLpNeutral (idx ?index)))
      (body))
    (rule prime-lp-neutral-app
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head (PrimeLpNeutral (App ?function ?argument)))
      (body (PrimeLpNeutral ?function)))
    (rule prime-lp-normalize-u0
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head (PrimeLpNormalizes U0 U0))
      (body))
    (rule prime-lp-normalize-u1
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head (PrimeLpNormalizes U1 U1))
      (body))
    (rule prime-lp-normalize-index
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head (PrimeLpNormalizes (idx ?index) (idx ?index)))
      (body))
    (rule prime-lp-normalize-pi
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head
        (PrimeLpNormalizes (Pi ?domain ?body)
          (Pi ?normal-domain ?normal-body)))
      (body
        (PrimeLpNormalizes ?domain ?normal-domain)
        (PrimeLpNormalizes ?body ?normal-body)))
    (rule prime-lp-normalize-lam-stable
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head
        (PrimeLpNormalizes (Lam ?domain ?body)
          (Lam ?normal-domain ?normal-body)))
      (body
        (PrimeLpNormalizes ?domain ?normal-domain)
        (PrimeLpNormalizes ?body ?normal-body)
        (PrimeLpEtaStableBody ?normal-body)))
    (rule prime-lp-normalize-lam-eta
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head
        (PrimeLpNormalizes (Lam ?domain ?body) ?lowered-function))
      (body
        (PrimeLpNormalizes ?domain ?normal-domain)
        (PrimeLpNormalizes ?body
          (App ?normal-function (idx PrimeZero)))
        (PrimeLpUnusedAt PrimeZero ?normal-function)
        (PrimeLpLower PrimeZero ?normal-function ?lowered-function)))
    (rule prime-lp-normalize-app-neutral
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head
        (PrimeLpNormalizes (App ?function ?argument)
          (App ?normal-function ?normal-argument)))
      (body
        (PrimeLpNormalizes ?function ?normal-function)
        (PrimeLpNeutral ?normal-function)
        (PrimeLpNormalizes ?argument ?normal-argument)))
    (rule prime-lp-normalize-app-beta
      (nik-rule-frame-v1
        (native-projection native-beta-eta-normalization)
        (role calculus))
      (head (PrimeLpNormalizes (App ?function ?argument) ?normal))
      (body
        (PrimeLpNormalizes ?function (Lam ?domain ?body))
        (PrimeLpNormalizes ?argument ?normal-argument)
        (PrimeLpSubstitute ?normal-argument PrimeZero ?body ?contracted)
        (PrimeLpNormalizes ?contracted ?normal)))
    (rule prime-lp-convertible
      (nik-rule-frame-v1
        (native-projection native-beta-eta-conversion)
        (role calculus))
      (head (PrimeLpConvertible ?left ?right))
      (body
        (PrimeLpNormalizes ?left ?normal)
        (PrimeLpNormalizes ?right ?normal)))

    (rule prime-lp-lookup-zero
      (nik-rule-frame-v1
        (native-projection native-context-lookup)
        (role calculus))
      (head
        (PrimeLpLookup (PrimeCtxCons ?domain ?context)
          PrimeZero ?type))
      (body
        (PrimeLpShift (PrimeSucc PrimeZero) PrimeZero ?domain ?type)))
    (rule prime-lp-lookup-successor
      (nik-rule-frame-v1
        (native-projection native-context-lookup)
        (role calculus))
      (head
        (PrimeLpLookup (PrimeCtxCons ?domain ?context)
          (PrimeSucc ?index) ?type))
      (body
        (PrimeLpLookup ?context ?index ?outer-type)
        (PrimeLpShift (PrimeSucc PrimeZero) PrimeZero
          ?outer-type ?type)))
    (rule prime-lp-context-empty
      (nik-rule-frame-v1
        (native-projection native-context-formation)
        (role calculus))
      (head (PrimeLpContextValid PrimeCtxNil))
      (body))
    (rule prime-lp-context-bind
      (nik-rule-frame-v1
        (native-projection native-context-formation)
        (role calculus))
      (head (PrimeLpContextValid (PrimeCtxCons ?domain ?context)))
      (body
        (PrimeLpContextValid ?context)
        (PrimeLpOrdinaryType ?context ?domain)))
    (rule prime-lp-ordinary-type
      (nik-rule-frame-v1
        (native-projection native-type-formation)
        (role calculus))
      (head (PrimeLpOrdinaryType ?context ?type))
      (body (PrimeLpSynthCore ?context ?type U1)))

    (rule prime-lp-synth-u0
      (nik-rule-frame-v1
        (native-projection native-dependent-synthesis)
        (role calculus))
      (head (PrimeLpSynthCore ?context U0 U1))
      (body))
    (rule prime-lp-synth-index
      (nik-rule-frame-v1
        (native-projection native-dependent-synthesis)
        (role calculus))
      (head (PrimeLpSynthCore ?context (idx ?index) ?type))
      (body (PrimeLpLookup ?context ?index ?type)))
    (rule prime-lp-synth-pi
      (nik-rule-frame-v1
        (native-projection native-dependent-synthesis)
        (role calculus))
      (head (PrimeLpSynthCore ?context (Pi ?domain ?body) U1))
      (body
        (PrimeLpOrdinaryType ?context ?domain)
        (PrimeLpOrdinaryType
          (PrimeCtxCons ?domain ?context) ?body)))
    (rule prime-lp-synth-lam
      (nik-rule-frame-v1
        (native-projection native-dependent-synthesis)
        (role calculus))
      (head
        (PrimeLpSynthCore ?context (Lam ?domain ?body)
          (Pi ?domain ?body-type)))
      (body
        (PrimeLpOrdinaryType ?context ?domain)
        (PrimeLpSynthCore
          (PrimeCtxCons ?domain ?context) ?body ?body-type)
        (PrimeLpOrdinaryType
          (PrimeCtxCons ?domain ?context) ?body-type)))
    (rule prime-lp-synth-app
      (nik-rule-frame-v1
        (native-projection native-dependent-synthesis)
        (role calculus))
      (head
        (PrimeLpSynthCore ?context (App ?function ?argument) ?result-type))
      (body
        (PrimeLpSynthCore ?context ?function ?function-type)
        (PrimeLpNormalizes ?function-type (Pi ?domain ?body))
        (PrimeLpCheckCore ?context ?argument ?domain)
        (PrimeLpSubstitute ?argument PrimeZero ?body ?result-type)))

    (rule prime-lp-not-lam-u0
      (nik-rule-frame-v1
        (native-projection native-bidirectional-checking)
        (role calculus))
      (head (PrimeLpNotLam U0))
      (body))
    (rule prime-lp-not-lam-u1
      (nik-rule-frame-v1
        (native-projection native-bidirectional-checking)
        (role calculus))
      (head (PrimeLpNotLam U1))
      (body))
    (rule prime-lp-not-lam-index
      (nik-rule-frame-v1
        (native-projection native-bidirectional-checking)
        (role calculus))
      (head (PrimeLpNotLam (idx ?index)))
      (body))
    (rule prime-lp-not-lam-pi
      (nik-rule-frame-v1
        (native-projection native-bidirectional-checking)
        (role calculus))
      (head (PrimeLpNotLam (Pi ?domain ?body)))
      (body))
    (rule prime-lp-not-lam-app
      (nik-rule-frame-v1
        (native-projection native-bidirectional-checking)
        (role calculus))
      (head (PrimeLpNotLam (App ?function ?argument)))
      (body))
    (rule prime-lp-check-lam
      (nik-rule-frame-v1
        (native-projection native-bidirectional-checking)
        (role calculus))
      (head
        (PrimeLpCheckCore ?context (Lam ?annotation ?term) ?expected))
      (body
        (PrimeLpOrdinaryType ?context ?annotation)
        (PrimeLpNormalizes ?expected (Pi ?domain ?body))
        (PrimeLpConvertible ?annotation ?domain)
        (PrimeLpCheckCore
          (PrimeCtxCons ?domain ?context) ?term ?body)))
    (rule prime-lp-check-synthesized
      (nik-rule-frame-v1
        (native-projection native-bidirectional-checking)
        (role calculus))
      (head (PrimeLpCheckCore ?context ?term ?expected))
      (body
        (PrimeLpNotLam ?term)
        (PrimeLpSynthCore ?context ?term ?inferred)
        (PrimeLpConvertible ?inferred ?expected)))

    (rule prime-lp-judge-synth
      (nik-rule-frame-v1
        (native-projection native-open-lambda-pi-judgment)
        (role calculus))
      (head
        (PrimeLpJudges
          (PSynth (PrimeScoped ?context ?term) ?type) PEstablished))
      (body
        (PrimeLpContextValid ?context)
        (PrimeLpSynthCore ?context ?term ?type)))
    (rule prime-lp-judge-check
      (nik-rule-frame-v1
        (native-projection native-open-lambda-pi-judgment)
        (role calculus))
      (head
        (PrimeLpJudges
          (PCheck (PrimeScoped ?context ?term) ?expected) PEstablished))
      (body
        (PrimeLpContextValid ?context)
        (PrimeLpOrdinaryType ?context ?expected)
        (PrimeLpCheckCore ?context ?term ?expected)))
    (rule prime-lp-judge-check-upper-sort
      (nik-rule-frame-v1
        (native-projection native-open-lambda-pi-judgment)
        (role calculus))
      (head
        (PrimeLpJudges
          (PCheck (PrimeScoped ?context ?term) U1) PEstablished))
      (body
        (PrimeLpContextValid ?context)
        (PrimeLpCheckCore ?context ?term U1)))
    (rule prime-lp-judge-convert-upper-sort
      (nik-rule-frame-v1
        (native-projection native-open-lambda-pi-judgment)
        (role calculus))
      (head
        (PrimeLpJudges
          (PConvert (PrimeScoped ?context U1)
            (PrimeScoped ?context U1)) PEstablished))
      (body (PrimeLpContextValid ?context)))
    (rule prime-lp-judge-convert
      (nik-rule-frame-v1
        (native-projection native-open-lambda-pi-judgment)
        (role calculus))
      (head
        (PrimeLpJudges
          (PConvert (PrimeScoped ?context ?left)
            (PrimeScoped ?context ?right)) PEstablished))
      (body
        (PrimeLpContextValid ?context)
        (PrimeLpSynthCore ?context ?left ?left-type)
        (PrimeLpSynthCore ?context ?right ?right-type)
        (PrimeLpConvertible ?left-type ?right-type)
        (PrimeLpConvertible ?left ?right)))
  ))
