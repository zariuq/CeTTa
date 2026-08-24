; Authored finite-Horn presentation of Prime's open regular kernel.
; Context entries, bound occurrences, and closed universe levels use
; structural Peano data here; the native direct judge uses compact integers
; for the same intrinsic objects.
; The public judgments keep the existing PSynth/PCheck/PConvert arities and
; carry the open context inside `PrimeScoped`.
(gslt-presentation-v1 prime-open-regular-kernel-v1
  (signature
    (operator PrimeScoped 2)
    (operator PrimeCtxCons 2)
    (operator PrimeSucc 1)
    (operator idx 1)
    (operator Sort 1)
    (operator LevelSucc 1)
    (operator LevelMax 2)
    (operator Pi 2)
    (operator Sigma 2)
    (operator Id 3)
    (operator Lam 2)
    (operator App 2)
    (operator Pair 2)
    (operator Fst 1)
    (operator Snd 1)
    (operator Refl 1)
    (operator PSynth 2)
    (operator PCheck 2)
    (operator PConvert 2)
    (operator PrimeRegularJudges 2)
    (operator PrimeRegularNatLt 2)
    (operator PrimeRegularNatLe 2)
    (operator PrimeRegularNatNe 2)
    (operator PrimeRegularNatAdd 3)
    (operator PrimeRegularRemoveAfter 3)
    (operator PrimeRegularLevel 1)
    (operator PrimeRegularLevelMax 3)
    (operator PrimeRegularLevelLt 2)
    (operator PrimeRegularLevelNormalizes 2)
    (operator PrimeRegularUniverseSort 1)
    (operator PrimeRegularSortSuccessor 2)
    (operator PrimeRegularSortJoin 3)
    (operator PrimeRegularSortLevel 2)
    (operator PrimeRegularShift 4)
    (operator PrimeRegularLower 3)
    (operator PrimeRegularUsesAt 2)
    (operator PrimeRegularUnusedAt 2)
    (operator PrimeRegularNotIndexZero 1)
    (operator PrimeRegularEtaStableBody 1)
    (operator PrimeRegularSubstitute 4)
    (operator PrimeRegularNormalizes 2)
    (operator PrimeRegularNeutral 1)
    (operator PrimeRegularConvertible 2)
    (operator PrimeRegularLookup 3)
    (operator PrimeRegularContextValid 1)
    (operator PrimeRegularOrdinaryType 2)
    (operator PrimeRegularTypeSort 3)
    (operator PrimeRegularAssignable 2)
    (operator PrimeRegularSynthCore 3)
    (operator PrimeRegularCheckCore 3)
    (operator PrimeRegularNotLam 1)
  )
  (equations)
  (rewrites
    (rule prime-regular-nat-lt-zero
      (head (PrimeRegularNatLt PrimeZero (PrimeSucc ?right)))
      (body))
    (rule prime-regular-nat-lt-successor
      (head
        (PrimeRegularNatLt (PrimeSucc ?left) (PrimeSucc ?right)))
      (body (PrimeRegularNatLt ?left ?right)))
    (rule prime-regular-nat-le-zero
      (head (PrimeRegularNatLe PrimeZero ?right))
      (body))
    (rule prime-regular-nat-le-successor
      (head
        (PrimeRegularNatLe (PrimeSucc ?left) (PrimeSucc ?right)))
      (body (PrimeRegularNatLe ?left ?right)))
    (rule prime-regular-nat-ne-zero-successor
      (head (PrimeRegularNatNe PrimeZero (PrimeSucc ?right)))
      (body))
    (rule prime-regular-nat-ne-successor-zero
      (head (PrimeRegularNatNe (PrimeSucc ?left) PrimeZero))
      (body))
    (rule prime-regular-nat-ne-successor
      (head
        (PrimeRegularNatNe (PrimeSucc ?left) (PrimeSucc ?right)))
      (body (PrimeRegularNatNe ?left ?right)))
    (rule prime-regular-nat-add-zero
      (head (PrimeRegularNatAdd PrimeZero ?right ?right))
      (body))
    (rule prime-regular-nat-add-successor
      (head
        (PrimeRegularNatAdd (PrimeSucc ?left) ?right (PrimeSucc ?sum)))
      (body (PrimeRegularNatAdd ?left ?right ?sum)))
    (rule prime-regular-remove-after-zero
      (head
        (PrimeRegularRemoveAfter PrimeZero (PrimeSucc ?index) ?index))
      (body))
    (rule prime-regular-remove-after-successor
      (head
        (PrimeRegularRemoveAfter (PrimeSucc ?depth) (PrimeSucc ?index)
          (PrimeSucc ?result)))
      (body (PrimeRegularRemoveAfter ?depth ?index ?result)))

    (rule prime-regular-level-zero
      (head (PrimeRegularLevel LevelZero))
      (body))
    (rule prime-regular-level-successor
      (head (PrimeRegularLevel (LevelSucc ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-level-maximum
      (head (PrimeRegularLevel (LevelMax ?left ?right)))
      (body
        (PrimeRegularLevel ?left)
        (PrimeRegularLevel ?right)))
    (rule prime-regular-level-max-zero-left
      (head (PrimeRegularLevelMax LevelZero ?right ?right))
      (body (PrimeRegularLevel ?right)))
    (rule prime-regular-level-max-successor-zero
      (head
        (PrimeRegularLevelMax (LevelSucc ?left) LevelZero
          (LevelSucc ?left)))
      (body (PrimeRegularLevel ?left)))
    (rule prime-regular-level-max-successors
      (head
        (PrimeRegularLevelMax (LevelSucc ?left) (LevelSucc ?right)
          (LevelSucc ?maximum)))
      (body (PrimeRegularLevelMax ?left ?right ?maximum)))
    (rule prime-regular-level-lt-zero-successor
      (head (PrimeRegularLevelLt LevelZero (LevelSucc ?right)))
      (body (PrimeRegularLevel ?right)))
    (rule prime-regular-level-lt-successors
      (head
        (PrimeRegularLevelLt (LevelSucc ?left) (LevelSucc ?right)))
      (body (PrimeRegularLevelLt ?left ?right)))
    (rule prime-regular-level-normalize-zero
      (head (PrimeRegularLevelNormalizes LevelZero LevelZero))
      (body))
    (rule prime-regular-level-normalize-successor
      (head
        (PrimeRegularLevelNormalizes (LevelSucc ?level)
          (LevelSucc ?normal)))
      (body (PrimeRegularLevelNormalizes ?level ?normal)))
    (rule prime-regular-level-normalize-maximum
      (head
        (PrimeRegularLevelNormalizes (LevelMax ?left ?right) ?normal))
      (body
        (PrimeRegularLevelNormalizes ?left ?normal-left)
        (PrimeRegularLevelNormalizes ?right ?normal-right)
        (PrimeRegularLevelMax ?normal-left ?normal-right ?normal)))
    (rule prime-regular-universe-sort-u1
      (head (PrimeRegularUniverseSort U1))
      (body))
    (rule prime-regular-universe-sort-explicit
      (head (PrimeRegularUniverseSort (Sort ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-sort-successor-u1
      (head
        (PrimeRegularSortSuccessor U1 (Sort (LevelSucc LevelZero))))
      (body))
    (rule prime-regular-sort-successor-explicit
      (head
        (PrimeRegularSortSuccessor (Sort ?level)
          (Sort (LevelSucc ?level))))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-sort-join-u1
      (head (PrimeRegularSortJoin U1 U1 U1))
      (body))
    (rule prime-regular-sort-join-u1-explicit
      (head
        (PrimeRegularSortJoin U1 (Sort ?right)
          (Sort (LevelMax LevelZero ?right))))
      (body (PrimeRegularLevel ?right)))
    (rule prime-regular-sort-join-explicit-u1
      (head
        (PrimeRegularSortJoin (Sort ?left) U1
          (Sort (LevelMax ?left LevelZero))))
      (body (PrimeRegularLevel ?left)))
    (rule prime-regular-sort-join-explicit
      (head
        (PrimeRegularSortJoin (Sort ?left) (Sort ?right)
          (Sort (LevelMax ?left ?right))))
      (body
        (PrimeRegularLevel ?left)
        (PrimeRegularLevel ?right)))

    (rule prime-regular-shift-u0
      (head (PrimeRegularShift ?amount ?cutoff U0 U0))
      (body))
    (rule prime-regular-shift-u1
      (head (PrimeRegularShift ?amount ?cutoff U1 U1))
      (body))
    (rule prime-regular-shift-sort
      (head
        (PrimeRegularShift ?amount ?cutoff (Sort ?level) (Sort ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-shift-index-below
      (head
        (PrimeRegularShift ?amount ?cutoff (idx ?index) (idx ?index)))
      (body (PrimeRegularNatLt ?index ?cutoff)))
    (rule prime-regular-shift-index-at-or-above
      (head
        (PrimeRegularShift ?amount ?cutoff (idx ?index) (idx ?shifted)))
      (body
        (PrimeRegularNatLe ?cutoff ?index)
        (PrimeRegularNatAdd ?amount ?index ?shifted)))
    (rule prime-regular-shift-pi
      (head
        (PrimeRegularShift ?amount ?cutoff (Pi ?domain ?body)
          (Pi ?shifted-domain ?shifted-body)))
      (body
        (PrimeRegularShift ?amount ?cutoff ?domain ?shifted-domain)
        (PrimeRegularShift ?amount (PrimeSucc ?cutoff)
          ?body ?shifted-body)))
    (rule prime-regular-shift-sigma
      (head
        (PrimeRegularShift ?amount ?cutoff (Sigma ?domain ?body)
          (Sigma ?shifted-domain ?shifted-body)))
      (body
        (PrimeRegularShift ?amount ?cutoff ?domain ?shifted-domain)
        (PrimeRegularShift ?amount (PrimeSucc ?cutoff)
          ?body ?shifted-body)))
    (rule prime-regular-shift-lam
      (head
        (PrimeRegularShift ?amount ?cutoff (Lam ?domain ?body)
          (Lam ?shifted-domain ?shifted-body)))
      (body
        (PrimeRegularShift ?amount ?cutoff ?domain ?shifted-domain)
        (PrimeRegularShift ?amount (PrimeSucc ?cutoff)
          ?body ?shifted-body)))
    (rule prime-regular-shift-app
      (head
        (PrimeRegularShift ?amount ?cutoff (App ?function ?argument)
          (App ?shifted-function ?shifted-argument)))
      (body
        (PrimeRegularShift ?amount ?cutoff ?function ?shifted-function)
        (PrimeRegularShift ?amount ?cutoff ?argument ?shifted-argument)))
    (rule prime-regular-shift-id
      (head
        (PrimeRegularShift ?amount ?cutoff (Id ?carrier ?left ?right)
          (Id ?shifted-carrier ?shifted-left ?shifted-right)))
      (body
        (PrimeRegularShift ?amount ?cutoff ?carrier ?shifted-carrier)
        (PrimeRegularShift ?amount ?cutoff ?left ?shifted-left)
        (PrimeRegularShift ?amount ?cutoff ?right ?shifted-right)))
    (rule prime-regular-shift-pair
      (head
        (PrimeRegularShift ?amount ?cutoff (Pair ?first ?second)
          (Pair ?shifted-first ?shifted-second)))
      (body
        (PrimeRegularShift ?amount ?cutoff ?first ?shifted-first)
        (PrimeRegularShift ?amount ?cutoff ?second ?shifted-second)))
    (rule prime-regular-shift-fst
      (head
        (PrimeRegularShift ?amount ?cutoff (Fst ?pair)
          (Fst ?shifted-pair)))
      (body (PrimeRegularShift ?amount ?cutoff ?pair ?shifted-pair)))
    (rule prime-regular-shift-snd
      (head
        (PrimeRegularShift ?amount ?cutoff (Snd ?pair)
          (Snd ?shifted-pair)))
      (body (PrimeRegularShift ?amount ?cutoff ?pair ?shifted-pair)))
    (rule prime-regular-shift-refl
      (head
        (PrimeRegularShift ?amount ?cutoff (Refl ?term)
          (Refl ?shifted-term)))
      (body (PrimeRegularShift ?amount ?cutoff ?term ?shifted-term)))

    (rule prime-regular-lower-u0
      (head (PrimeRegularLower ?cutoff U0 U0))
      (body))
    (rule prime-regular-lower-u1
      (head (PrimeRegularLower ?cutoff U1 U1))
      (body))
    (rule prime-regular-lower-sort
      (head (PrimeRegularLower ?cutoff (Sort ?level) (Sort ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-lower-index-below
      (head (PrimeRegularLower ?cutoff (idx ?index) (idx ?index)))
      (body (PrimeRegularNatLt ?index ?cutoff)))
    (rule prime-regular-lower-index-at-or-above
      (head
        (PrimeRegularLower ?cutoff (idx (PrimeSucc ?index)) (idx ?index)))
      (body (PrimeRegularNatLe ?cutoff (PrimeSucc ?index))))
    (rule prime-regular-lower-pi
      (head
        (PrimeRegularLower ?cutoff (Pi ?domain ?body)
          (Pi ?lowered-domain ?lowered-body)))
      (body
        (PrimeRegularLower ?cutoff ?domain ?lowered-domain)
        (PrimeRegularLower (PrimeSucc ?cutoff) ?body ?lowered-body)))
    (rule prime-regular-lower-sigma
      (head
        (PrimeRegularLower ?cutoff (Sigma ?domain ?body)
          (Sigma ?lowered-domain ?lowered-body)))
      (body
        (PrimeRegularLower ?cutoff ?domain ?lowered-domain)
        (PrimeRegularLower (PrimeSucc ?cutoff) ?body ?lowered-body)))
    (rule prime-regular-lower-lam
      (head
        (PrimeRegularLower ?cutoff (Lam ?domain ?body)
          (Lam ?lowered-domain ?lowered-body)))
      (body
        (PrimeRegularLower ?cutoff ?domain ?lowered-domain)
        (PrimeRegularLower (PrimeSucc ?cutoff) ?body ?lowered-body)))
    (rule prime-regular-lower-app
      (head
        (PrimeRegularLower ?cutoff (App ?function ?argument)
          (App ?lowered-function ?lowered-argument)))
      (body
        (PrimeRegularLower ?cutoff ?function ?lowered-function)
        (PrimeRegularLower ?cutoff ?argument ?lowered-argument)))
    (rule prime-regular-lower-id
      (head
        (PrimeRegularLower ?cutoff (Id ?carrier ?left ?right)
          (Id ?lowered-carrier ?lowered-left ?lowered-right)))
      (body
        (PrimeRegularLower ?cutoff ?carrier ?lowered-carrier)
        (PrimeRegularLower ?cutoff ?left ?lowered-left)
        (PrimeRegularLower ?cutoff ?right ?lowered-right)))
    (rule prime-regular-lower-pair
      (head
        (PrimeRegularLower ?cutoff (Pair ?first ?second)
          (Pair ?lowered-first ?lowered-second)))
      (body
        (PrimeRegularLower ?cutoff ?first ?lowered-first)
        (PrimeRegularLower ?cutoff ?second ?lowered-second)))
    (rule prime-regular-lower-fst
      (head
        (PrimeRegularLower ?cutoff (Fst ?pair) (Fst ?lowered-pair)))
      (body (PrimeRegularLower ?cutoff ?pair ?lowered-pair)))
    (rule prime-regular-lower-snd
      (head
        (PrimeRegularLower ?cutoff (Snd ?pair) (Snd ?lowered-pair)))
      (body (PrimeRegularLower ?cutoff ?pair ?lowered-pair)))
    (rule prime-regular-lower-refl
      (head
        (PrimeRegularLower ?cutoff (Refl ?term) (Refl ?lowered-term)))
      (body (PrimeRegularLower ?cutoff ?term ?lowered-term)))

    (rule prime-regular-uses-index
      (head (PrimeRegularUsesAt ?index (idx ?index)))
      (body))
    (rule prime-regular-uses-pi-domain
      (head (PrimeRegularUsesAt ?index (Pi ?domain ?body)))
      (body (PrimeRegularUsesAt ?index ?domain)))
    (rule prime-regular-uses-pi-body
      (head (PrimeRegularUsesAt ?index (Pi ?domain ?body)))
      (body (PrimeRegularUsesAt (PrimeSucc ?index) ?body)))
    (rule prime-regular-uses-sigma-domain
      (head (PrimeRegularUsesAt ?index (Sigma ?domain ?body)))
      (body (PrimeRegularUsesAt ?index ?domain)))
    (rule prime-regular-uses-sigma-body
      (head (PrimeRegularUsesAt ?index (Sigma ?domain ?body)))
      (body (PrimeRegularUsesAt (PrimeSucc ?index) ?body)))
    (rule prime-regular-uses-lam-domain
      (head (PrimeRegularUsesAt ?index (Lam ?domain ?body)))
      (body (PrimeRegularUsesAt ?index ?domain)))
    (rule prime-regular-uses-lam-body
      (head (PrimeRegularUsesAt ?index (Lam ?domain ?body)))
      (body (PrimeRegularUsesAt (PrimeSucc ?index) ?body)))
    (rule prime-regular-uses-app-function
      (head (PrimeRegularUsesAt ?index (App ?function ?argument)))
      (body (PrimeRegularUsesAt ?index ?function)))
    (rule prime-regular-uses-app-argument
      (head (PrimeRegularUsesAt ?index (App ?function ?argument)))
      (body (PrimeRegularUsesAt ?index ?argument)))
    (rule prime-regular-uses-id-carrier
      (head (PrimeRegularUsesAt ?index (Id ?carrier ?left ?right)))
      (body (PrimeRegularUsesAt ?index ?carrier)))
    (rule prime-regular-uses-id-left
      (head (PrimeRegularUsesAt ?index (Id ?carrier ?left ?right)))
      (body (PrimeRegularUsesAt ?index ?left)))
    (rule prime-regular-uses-id-right
      (head (PrimeRegularUsesAt ?index (Id ?carrier ?left ?right)))
      (body (PrimeRegularUsesAt ?index ?right)))
    (rule prime-regular-uses-pair-first
      (head (PrimeRegularUsesAt ?index (Pair ?first ?second)))
      (body (PrimeRegularUsesAt ?index ?first)))
    (rule prime-regular-uses-pair-second
      (head (PrimeRegularUsesAt ?index (Pair ?first ?second)))
      (body (PrimeRegularUsesAt ?index ?second)))
    (rule prime-regular-uses-fst
      (head (PrimeRegularUsesAt ?index (Fst ?pair)))
      (body (PrimeRegularUsesAt ?index ?pair)))
    (rule prime-regular-uses-snd
      (head (PrimeRegularUsesAt ?index (Snd ?pair)))
      (body (PrimeRegularUsesAt ?index ?pair)))
    (rule prime-regular-uses-refl
      (head (PrimeRegularUsesAt ?index (Refl ?term)))
      (body (PrimeRegularUsesAt ?index ?term)))
    (rule prime-regular-unused-u0
      (head (PrimeRegularUnusedAt ?index U0))
      (body))
    (rule prime-regular-unused-u1
      (head (PrimeRegularUnusedAt ?index U1))
      (body))
    (rule prime-regular-unused-sort
      (head (PrimeRegularUnusedAt ?index (Sort ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-unused-index
      (head (PrimeRegularUnusedAt ?index (idx ?other)))
      (body (PrimeRegularNatNe ?index ?other)))
    (rule prime-regular-unused-pi
      (head (PrimeRegularUnusedAt ?index (Pi ?domain ?body)))
      (body
        (PrimeRegularUnusedAt ?index ?domain)
        (PrimeRegularUnusedAt (PrimeSucc ?index) ?body)))
    (rule prime-regular-unused-sigma
      (head (PrimeRegularUnusedAt ?index (Sigma ?domain ?body)))
      (body
        (PrimeRegularUnusedAt ?index ?domain)
        (PrimeRegularUnusedAt (PrimeSucc ?index) ?body)))
    (rule prime-regular-unused-lam
      (head (PrimeRegularUnusedAt ?index (Lam ?domain ?body)))
      (body
        (PrimeRegularUnusedAt ?index ?domain)
        (PrimeRegularUnusedAt (PrimeSucc ?index) ?body)))
    (rule prime-regular-unused-app
      (head (PrimeRegularUnusedAt ?index (App ?function ?argument)))
      (body
        (PrimeRegularUnusedAt ?index ?function)
        (PrimeRegularUnusedAt ?index ?argument)))
    (rule prime-regular-unused-id
      (head (PrimeRegularUnusedAt ?index (Id ?carrier ?left ?right)))
      (body
        (PrimeRegularUnusedAt ?index ?carrier)
        (PrimeRegularUnusedAt ?index ?left)
        (PrimeRegularUnusedAt ?index ?right)))
    (rule prime-regular-unused-pair
      (head (PrimeRegularUnusedAt ?index (Pair ?first ?second)))
      (body
        (PrimeRegularUnusedAt ?index ?first)
        (PrimeRegularUnusedAt ?index ?second)))
    (rule prime-regular-unused-fst
      (head (PrimeRegularUnusedAt ?index (Fst ?pair)))
      (body (PrimeRegularUnusedAt ?index ?pair)))
    (rule prime-regular-unused-snd
      (head (PrimeRegularUnusedAt ?index (Snd ?pair)))
      (body (PrimeRegularUnusedAt ?index ?pair)))
    (rule prime-regular-unused-refl
      (head (PrimeRegularUnusedAt ?index (Refl ?term)))
      (body (PrimeRegularUnusedAt ?index ?term)))

    (rule prime-regular-not-index-zero-u0
      (head (PrimeRegularNotIndexZero U0))
      (body))
    (rule prime-regular-not-index-zero-u1
      (head (PrimeRegularNotIndexZero U1))
      (body))
    (rule prime-regular-not-index-zero-sort
      (head (PrimeRegularNotIndexZero (Sort ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-not-index-zero-successor
      (head (PrimeRegularNotIndexZero (idx (PrimeSucc ?index))))
      (body))
    (rule prime-regular-not-index-zero-pi
      (head (PrimeRegularNotIndexZero (Pi ?domain ?body)))
      (body))
    (rule prime-regular-not-index-zero-lam
      (head (PrimeRegularNotIndexZero (Lam ?domain ?body)))
      (body))
    (rule prime-regular-not-index-zero-app
      (head (PrimeRegularNotIndexZero (App ?function ?argument)))
      (body))
    (rule prime-regular-not-index-zero-sigma
      (head (PrimeRegularNotIndexZero (Sigma ?domain ?body)))
      (body))
    (rule prime-regular-not-index-zero-id
      (head (PrimeRegularNotIndexZero (Id ?carrier ?left ?right)))
      (body))
    (rule prime-regular-not-index-zero-pair
      (head (PrimeRegularNotIndexZero (Pair ?first ?second)))
      (body))
    (rule prime-regular-not-index-zero-fst
      (head (PrimeRegularNotIndexZero (Fst ?pair)))
      (body))
    (rule prime-regular-not-index-zero-snd
      (head (PrimeRegularNotIndexZero (Snd ?pair)))
      (body))
    (rule prime-regular-not-index-zero-refl
      (head (PrimeRegularNotIndexZero (Refl ?term)))
      (body))
    (rule prime-regular-eta-stable-u0
      (head (PrimeRegularEtaStableBody U0))
      (body))
    (rule prime-regular-eta-stable-u1
      (head (PrimeRegularEtaStableBody U1))
      (body))
    (rule prime-regular-eta-stable-sort
      (head (PrimeRegularEtaStableBody (Sort ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-eta-stable-index
      (head (PrimeRegularEtaStableBody (idx ?index)))
      (body))
    (rule prime-regular-eta-stable-pi
      (head (PrimeRegularEtaStableBody (Pi ?domain ?body)))
      (body))
    (rule prime-regular-eta-stable-lam
      (head (PrimeRegularEtaStableBody (Lam ?domain ?body)))
      (body))
    (rule prime-regular-eta-stable-app-other-argument
      (head (PrimeRegularEtaStableBody (App ?function ?argument)))
      (body (PrimeRegularNotIndexZero ?argument)))
    (rule prime-regular-eta-stable-app-dependent-function
      (head
        (PrimeRegularEtaStableBody
          (App ?function (idx PrimeZero))))
      (body (PrimeRegularUsesAt PrimeZero ?function)))
    (rule prime-regular-eta-stable-sigma
      (head (PrimeRegularEtaStableBody (Sigma ?domain ?body)))
      (body))
    (rule prime-regular-eta-stable-id
      (head (PrimeRegularEtaStableBody (Id ?carrier ?left ?right)))
      (body))
    (rule prime-regular-eta-stable-pair
      (head (PrimeRegularEtaStableBody (Pair ?first ?second)))
      (body))
    (rule prime-regular-eta-stable-fst
      (head (PrimeRegularEtaStableBody (Fst ?pair)))
      (body))
    (rule prime-regular-eta-stable-snd
      (head (PrimeRegularEtaStableBody (Snd ?pair)))
      (body))
    (rule prime-regular-eta-stable-refl
      (head (PrimeRegularEtaStableBody (Refl ?term)))
      (body))

    (rule prime-regular-substitute-u0
      (head (PrimeRegularSubstitute ?argument ?depth U0 U0))
      (body))
    (rule prime-regular-substitute-u1
      (head (PrimeRegularSubstitute ?argument ?depth U1 U1))
      (body))
    (rule prime-regular-substitute-sort
      (head
        (PrimeRegularSubstitute ?argument ?depth
          (Sort ?level) (Sort ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-substitute-index-below
      (head
        (PrimeRegularSubstitute ?argument ?depth (idx ?index) (idx ?index)))
      (body (PrimeRegularNatLt ?index ?depth)))
    (rule prime-regular-substitute-index-equal
      (head
        (PrimeRegularSubstitute ?argument ?depth (idx ?depth) ?lifted))
      (body (PrimeRegularShift ?depth PrimeZero ?argument ?lifted)))
    (rule prime-regular-substitute-index-above
      (head
        (PrimeRegularSubstitute ?argument ?depth (idx ?index) (idx ?result)))
      (body (PrimeRegularRemoveAfter ?depth ?index ?result)))
    (rule prime-regular-substitute-pi
      (head
        (PrimeRegularSubstitute ?argument ?depth (Pi ?domain ?body)
          (Pi ?result-domain ?result-body)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?domain ?result-domain)
        (PrimeRegularSubstitute ?argument (PrimeSucc ?depth)
          ?body ?result-body)))
    (rule prime-regular-substitute-sigma
      (head
        (PrimeRegularSubstitute ?argument ?depth (Sigma ?domain ?body)
          (Sigma ?result-domain ?result-body)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?domain ?result-domain)
        (PrimeRegularSubstitute ?argument (PrimeSucc ?depth)
          ?body ?result-body)))
    (rule prime-regular-substitute-lam
      (head
        (PrimeRegularSubstitute ?argument ?depth (Lam ?domain ?body)
          (Lam ?result-domain ?result-body)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?domain ?result-domain)
        (PrimeRegularSubstitute ?argument (PrimeSucc ?depth)
          ?body ?result-body)))
    (rule prime-regular-substitute-app
      (head
        (PrimeRegularSubstitute ?argument ?depth (App ?function ?value)
          (App ?result-function ?result-value)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?function ?result-function)
        (PrimeRegularSubstitute ?argument ?depth ?value ?result-value)))
    (rule prime-regular-substitute-id
      (head
        (PrimeRegularSubstitute ?argument ?depth (Id ?carrier ?left ?right)
          (Id ?result-carrier ?result-left ?result-right)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?carrier ?result-carrier)
        (PrimeRegularSubstitute ?argument ?depth ?left ?result-left)
        (PrimeRegularSubstitute ?argument ?depth ?right ?result-right)))
    (rule prime-regular-substitute-pair
      (head
        (PrimeRegularSubstitute ?argument ?depth (Pair ?first ?second)
          (Pair ?result-first ?result-second)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?first ?result-first)
        (PrimeRegularSubstitute ?argument ?depth ?second ?result-second)))
    (rule prime-regular-substitute-fst
      (head
        (PrimeRegularSubstitute ?argument ?depth (Fst ?pair)
          (Fst ?result-pair)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?pair ?result-pair)))
    (rule prime-regular-substitute-snd
      (head
        (PrimeRegularSubstitute ?argument ?depth (Snd ?pair)
          (Snd ?result-pair)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?pair ?result-pair)))
    (rule prime-regular-substitute-refl
      (head
        (PrimeRegularSubstitute ?argument ?depth (Refl ?term)
          (Refl ?result-term)))
      (body
        (PrimeRegularSubstitute ?argument ?depth ?term ?result-term)))

    (rule prime-regular-neutral-index
      (head (PrimeRegularNeutral (idx ?index)))
      (body))
    (rule prime-regular-neutral-app
      (head (PrimeRegularNeutral (App ?function ?argument)))
      (body (PrimeRegularNeutral ?function)))
    (rule prime-regular-neutral-fst
      (head (PrimeRegularNeutral (Fst ?pair)))
      (body (PrimeRegularNeutral ?pair)))
    (rule prime-regular-neutral-snd
      (head (PrimeRegularNeutral (Snd ?pair)))
      (body (PrimeRegularNeutral ?pair)))
    (rule prime-regular-normalize-u0
      (head (PrimeRegularNormalizes U0 U0))
      (body))
    (rule prime-regular-normalize-u1
      (head (PrimeRegularNormalizes U1 U1))
      (body))
    (rule prime-regular-normalize-sort
      (head
        (PrimeRegularNormalizes (Sort ?level) (Sort ?normal-level)))
      (body (PrimeRegularLevelNormalizes ?level ?normal-level)))
    (rule prime-regular-normalize-index
      (head (PrimeRegularNormalizes (idx ?index) (idx ?index)))
      (body))
    (rule prime-regular-normalize-pi
      (head
        (PrimeRegularNormalizes (Pi ?domain ?body)
          (Pi ?normal-domain ?normal-body)))
      (body
        (PrimeRegularNormalizes ?domain ?normal-domain)
        (PrimeRegularNormalizes ?body ?normal-body)))
    (rule prime-regular-normalize-sigma
      (head
        (PrimeRegularNormalizes (Sigma ?domain ?body)
          (Sigma ?normal-domain ?normal-body)))
      (body
        (PrimeRegularNormalizes ?domain ?normal-domain)
        (PrimeRegularNormalizes ?body ?normal-body)))
    (rule prime-regular-normalize-id
      (head
        (PrimeRegularNormalizes (Id ?carrier ?left ?right)
          (Id ?normal-carrier ?normal-left ?normal-right)))
      (body
        (PrimeRegularNormalizes ?carrier ?normal-carrier)
        (PrimeRegularNormalizes ?left ?normal-left)
        (PrimeRegularNormalizes ?right ?normal-right)))
    (rule prime-regular-normalize-pair
      (head
        (PrimeRegularNormalizes (Pair ?first ?second)
          (Pair ?normal-first ?normal-second)))
      (body
        (PrimeRegularNormalizes ?first ?normal-first)
        (PrimeRegularNormalizes ?second ?normal-second)))
    (rule prime-regular-normalize-refl
      (head
        (PrimeRegularNormalizes (Refl ?term) (Refl ?normal-term)))
      (body (PrimeRegularNormalizes ?term ?normal-term)))
    (rule prime-regular-normalize-lam-stable
      (head
        (PrimeRegularNormalizes (Lam ?domain ?body)
          (Lam ?normal-domain ?normal-body)))
      (body
        (PrimeRegularNormalizes ?domain ?normal-domain)
        (PrimeRegularNormalizes ?body ?normal-body)
        (PrimeRegularEtaStableBody ?normal-body)))
    (rule prime-regular-normalize-lam-eta
      (head
        (PrimeRegularNormalizes (Lam ?domain ?body) ?lowered-function))
      (body
        (PrimeRegularNormalizes ?domain ?normal-domain)
        (PrimeRegularNormalizes ?body
          (App ?normal-function (idx PrimeZero)))
        (PrimeRegularUnusedAt PrimeZero ?normal-function)
        (PrimeRegularLower PrimeZero ?normal-function ?lowered-function)))
    (rule prime-regular-normalize-app-neutral
      (head
        (PrimeRegularNormalizes (App ?function ?argument)
          (App ?normal-function ?normal-argument)))
      (body
        (PrimeRegularNormalizes ?function ?normal-function)
        (PrimeRegularNeutral ?normal-function)
        (PrimeRegularNormalizes ?argument ?normal-argument)))
    (rule prime-regular-normalize-app-beta
      (head (PrimeRegularNormalizes (App ?function ?argument) ?normal))
      (body
        (PrimeRegularNormalizes ?function (Lam ?domain ?body))
        (PrimeRegularNormalizes ?argument ?normal-argument)
        (PrimeRegularSubstitute ?normal-argument PrimeZero ?body ?contracted)
        (PrimeRegularNormalizes ?contracted ?normal)))
    (rule prime-regular-normalize-fst-neutral
      (head
        (PrimeRegularNormalizes (Fst ?pair) (Fst ?normal-pair)))
      (body
        (PrimeRegularNormalizes ?pair ?normal-pair)
        (PrimeRegularNeutral ?normal-pair)))
    (rule prime-regular-normalize-fst-pair
      (head (PrimeRegularNormalizes (Fst ?pair) ?normal-first))
      (body
        (PrimeRegularNormalizes ?pair (Pair ?normal-first ?normal-second))))
    (rule prime-regular-normalize-snd-neutral
      (head
        (PrimeRegularNormalizes (Snd ?pair) (Snd ?normal-pair)))
      (body
        (PrimeRegularNormalizes ?pair ?normal-pair)
        (PrimeRegularNeutral ?normal-pair)))
    (rule prime-regular-normalize-snd-pair
      (head (PrimeRegularNormalizes (Snd ?pair) ?normal-second))
      (body
        (PrimeRegularNormalizes ?pair (Pair ?normal-first ?normal-second))))
    (rule prime-regular-convertible
      (head (PrimeRegularConvertible ?left ?right))
      (body
        (PrimeRegularNormalizes ?left ?normal)
        (PrimeRegularNormalizes ?right ?normal)))
    (rule prime-regular-convertible-u1-explicit
      (head (PrimeRegularConvertible U1 (Sort ?level)))
      (body (PrimeRegularLevelNormalizes ?level LevelZero)))
    (rule prime-regular-convertible-explicit-u1
      (head (PrimeRegularConvertible (Sort ?level) U1))
      (body (PrimeRegularLevelNormalizes ?level LevelZero)))

    (rule prime-regular-lookup-zero
      (head
        (PrimeRegularLookup (PrimeCtxCons ?domain ?context)
          PrimeZero ?type))
      (body
        (PrimeRegularShift (PrimeSucc PrimeZero) PrimeZero ?domain ?type)))
    (rule prime-regular-lookup-successor
      (head
        (PrimeRegularLookup (PrimeCtxCons ?domain ?context)
          (PrimeSucc ?index) ?type))
      (body
        (PrimeRegularLookup ?context ?index ?outer-type)
        (PrimeRegularShift (PrimeSucc PrimeZero) PrimeZero
          ?outer-type ?type)))
    (rule prime-regular-context-empty
      (head (PrimeRegularContextValid PrimeCtxNil))
      (body))
    (rule prime-regular-context-bind
      (head (PrimeRegularContextValid (PrimeCtxCons ?domain ?context)))
      (body
        (PrimeRegularContextValid ?context)
        (PrimeRegularOrdinaryType ?context ?domain)))
    (rule prime-regular-ordinary-type
      (head (PrimeRegularOrdinaryType ?context ?type))
      (body (PrimeRegularTypeSort ?context ?type ?sort)))
    (rule prime-regular-type-sort
      (head (PrimeRegularTypeSort ?context ?type ?sort))
      (body
        (PrimeRegularSynthCore ?context ?type ?sort)
        (PrimeRegularUniverseSort ?sort)))
    (rule prime-regular-assignable-convertible
      (head (PrimeRegularAssignable ?inferred ?expected))
      (body (PrimeRegularConvertible ?inferred ?expected)))
    (rule prime-regular-assignable-cumulative
      (head (PrimeRegularAssignable ?inferred ?expected))
      (body
        (PrimeRegularUniverseSort ?inferred)
        (PrimeRegularUniverseSort ?expected)
        (PrimeRegularSortLevel ?inferred ?inferred-level)
        (PrimeRegularSortLevel ?expected ?expected-level)
        (PrimeRegularLevelNormalizes ?inferred-level ?normal-inferred)
        (PrimeRegularLevelNormalizes ?expected-level ?normal-expected)
        (PrimeRegularLevelLt ?normal-inferred ?normal-expected)))

    (rule prime-regular-sort-level-u1
      (head (PrimeRegularSortLevel U1 LevelZero))
      (body))
    (rule prime-regular-sort-level-explicit
      (head (PrimeRegularSortLevel (Sort ?level) ?level))
      (body (PrimeRegularLevel ?level)))

    (rule prime-regular-synth-u0
      (head (PrimeRegularSynthCore ?context U0 U1))
      (body))
    (rule prime-regular-synth-u1
      (head
        (PrimeRegularSynthCore ?context U1
          (Sort (LevelSucc LevelZero))))
      (body))
    (rule prime-regular-synth-sort
      (head
        (PrimeRegularSynthCore ?context (Sort ?level) ?successor))
      (body
        (PrimeRegularUniverseSort (Sort ?level))
        (PrimeRegularSortSuccessor (Sort ?level) ?successor)))
    (rule prime-regular-synth-index
      (head (PrimeRegularSynthCore ?context (idx ?index) ?type))
      (body (PrimeRegularLookup ?context ?index ?type)))
    (rule prime-regular-synth-pi
      (head
        (PrimeRegularSynthCore ?context (Pi ?domain ?body) ?joined-sort))
      (body
        (PrimeRegularTypeSort ?context ?domain ?domain-sort)
        (PrimeRegularTypeSort
          (PrimeCtxCons ?domain ?context) ?body ?body-sort)
        (PrimeRegularSortJoin ?domain-sort ?body-sort ?joined-sort)))
    (rule prime-regular-synth-sigma
      (head
        (PrimeRegularSynthCore ?context (Sigma ?domain ?body) ?joined-sort))
      (body
        (PrimeRegularTypeSort ?context ?domain ?domain-sort)
        (PrimeRegularTypeSort
          (PrimeCtxCons ?domain ?context) ?body ?body-sort)
        (PrimeRegularSortJoin ?domain-sort ?body-sort ?joined-sort)))
    (rule prime-regular-synth-id
      (head
        (PrimeRegularSynthCore
          ?context (Id ?carrier ?left ?right) ?carrier-sort))
      (body
        (PrimeRegularTypeSort ?context ?carrier ?carrier-sort)
        (PrimeRegularCheckCore ?context ?left ?carrier)
        (PrimeRegularCheckCore ?context ?right ?carrier)))
    (rule prime-regular-synth-lam
      (head
        (PrimeRegularSynthCore ?context (Lam ?domain ?body)
          (Pi ?domain ?body-type)))
      (body
        (PrimeRegularOrdinaryType ?context ?domain)
        (PrimeRegularSynthCore
          (PrimeCtxCons ?domain ?context) ?body ?body-type)
        (PrimeRegularOrdinaryType
          (PrimeCtxCons ?domain ?context) ?body-type)))
    (rule prime-regular-synth-app
      (head
        (PrimeRegularSynthCore ?context (App ?function ?argument) ?result-type))
      (body
        (PrimeRegularSynthCore ?context ?function ?function-type)
        (PrimeRegularNormalizes ?function-type (Pi ?domain ?body))
        (PrimeRegularCheckCore ?context ?argument ?domain)
        (PrimeRegularSubstitute ?argument PrimeZero ?body ?result-type)))
    (rule prime-regular-synth-fst
      (head
        (PrimeRegularSynthCore ?context (Fst ?pair) ?domain))
      (body
        (PrimeRegularSynthCore ?context ?pair ?pair-type)
        (PrimeRegularNormalizes ?pair-type (Sigma ?domain ?codomain))))
    (rule prime-regular-synth-snd
      (head
        (PrimeRegularSynthCore ?context (Snd ?pair) ?result-type))
      (body
        (PrimeRegularSynthCore ?context ?pair ?pair-type)
        (PrimeRegularNormalizes ?pair-type (Sigma ?domain ?codomain))
        (PrimeRegularSubstitute (Fst ?pair) PrimeZero
          ?codomain ?result-type)))
    (rule prime-regular-synth-refl
      (head
        (PrimeRegularSynthCore ?context (Refl ?term)
          (Id ?carrier ?term ?term)))
      (body
        (PrimeRegularSynthCore ?context ?term ?carrier)
        (PrimeRegularOrdinaryType ?context ?carrier)))

    (rule prime-regular-not-lam-u0
      (head (PrimeRegularNotLam U0))
      (body))
    (rule prime-regular-not-lam-u1
      (head (PrimeRegularNotLam U1))
      (body))
    (rule prime-regular-not-lam-sort
      (head (PrimeRegularNotLam (Sort ?level)))
      (body (PrimeRegularLevel ?level)))
    (rule prime-regular-not-lam-index
      (head (PrimeRegularNotLam (idx ?index)))
      (body))
    (rule prime-regular-not-lam-pi
      (head (PrimeRegularNotLam (Pi ?domain ?body)))
      (body))
    (rule prime-regular-not-lam-app
      (head (PrimeRegularNotLam (App ?function ?argument)))
      (body))
    (rule prime-regular-not-lam-sigma
      (head (PrimeRegularNotLam (Sigma ?domain ?body)))
      (body))
    (rule prime-regular-not-lam-id
      (head (PrimeRegularNotLam (Id ?carrier ?left ?right)))
      (body))
    (rule prime-regular-not-lam-pair
      (head (PrimeRegularNotLam (Pair ?first ?second)))
      (body))
    (rule prime-regular-not-lam-fst
      (head (PrimeRegularNotLam (Fst ?pair)))
      (body))
    (rule prime-regular-not-lam-snd
      (head (PrimeRegularNotLam (Snd ?pair)))
      (body))
    (rule prime-regular-not-lam-refl
      (head (PrimeRegularNotLam (Refl ?term)))
      (body))
    (rule prime-regular-check-lam
      (head
        (PrimeRegularCheckCore ?context (Lam ?annotation ?term) ?expected))
      (body
        (PrimeRegularOrdinaryType ?context ?annotation)
        (PrimeRegularNormalizes ?expected (Pi ?domain ?body))
        (PrimeRegularConvertible ?annotation ?domain)
        (PrimeRegularCheckCore
          (PrimeCtxCons ?domain ?context) ?term ?body)))
    (rule prime-regular-check-pair
      (head
        (PrimeRegularCheckCore ?context (Pair ?first ?second) ?expected))
      (body
        (PrimeRegularNormalizes ?expected (Sigma ?domain ?codomain))
        (PrimeRegularCheckCore ?context ?first ?domain)
        (PrimeRegularSubstitute ?first PrimeZero
          ?codomain ?second-expected)
        (PrimeRegularCheckCore ?context ?second ?second-expected)))
    (rule prime-regular-check-synthesized
      (head (PrimeRegularCheckCore ?context ?term ?expected))
      (body
        (PrimeRegularNotLam ?term)
        (PrimeRegularSynthCore ?context ?term ?inferred)
        (PrimeRegularAssignable ?inferred ?expected)))

    (rule prime-regular-judge-synth
      (head
        (PrimeRegularJudges
          (PSynth (PrimeScoped ?context ?term) ?type) PEstablished))
      (body
        (PrimeRegularContextValid ?context)
        (PrimeRegularSynthCore ?context ?term ?type)))
    (rule prime-regular-judge-check
      (head
        (PrimeRegularJudges
          (PCheck (PrimeScoped ?context ?term) ?expected) PEstablished))
      (body
        (PrimeRegularContextValid ?context)
        (PrimeRegularOrdinaryType ?context ?expected)
        (PrimeRegularCheckCore ?context ?term ?expected)))
    (rule prime-regular-judge-convert
      (head
        (PrimeRegularJudges
          (PConvert (PrimeScoped ?context ?left)
            (PrimeScoped ?context ?right)) PEstablished))
      (body
        (PrimeRegularContextValid ?context)
        (PrimeRegularSynthCore ?context ?left ?left-type)
        (PrimeRegularSynthCore ?context ?right ?right-type)
        (PrimeRegularConvertible ?left-type ?right-type)
        (PrimeRegularConvertible ?left ?right)))
  ))
