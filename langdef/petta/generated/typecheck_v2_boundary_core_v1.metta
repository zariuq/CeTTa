(gslt-presentation-v1 petta-typecheck-v2-boundary-core-v1
  (nik-authority-frame-v1
    (mode direct-decision)
    (certificate-policy none)
    (fiber petta)
    (outcome-algebra (PTEstablished PTRefuted PTUndetermined PTIncomplete)
      (exclusive)
      (default PTUndetermined))
    (native-projection pending)
    (status AUTHORED_FRAGMENT)
    (commitments direct-computation certificate-free exclusive-outcomes explicit-default))
  (signature
    (operator PBBoundaryJoin 3)
    (operator PBConsumerRequirement 3)
    (operator PBGuardProvides 2)
    (operator PBBoundaryVisible 2)
  )
  (equations)
  (rewrites
    (rule petta-boundary-join-none-none
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNone PBNone PBNone))
      (body))
    (rule petta-boundary-join-none-nonvar
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNone PBNonvar PBNonvar))
      (body))
    (rule petta-boundary-join-none-proper-list
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNone PBProperList PBProperList))
      (body))
    (rule petta-boundary-join-none-nonempty-expression
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNone PBNonemptyExpression PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonvar-none
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNonvar PBNone PBNonvar))
      (body))
    (rule petta-boundary-join-nonvar-nonvar
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNonvar PBNonvar PBNonvar))
      (body))
    (rule petta-boundary-join-nonvar-proper-list
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNonvar PBProperList PBProperList))
      (body))
    (rule petta-boundary-join-nonvar-nonempty-expression
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNonvar PBNonemptyExpression PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-proper-list-none
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBProperList PBNone PBProperList))
      (body))
    (rule petta-boundary-join-proper-list-nonvar
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBProperList PBNonvar PBProperList))
      (body))
    (rule petta-boundary-join-proper-list-proper-list
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBProperList PBProperList PBProperList))
      (body))
    (rule petta-boundary-join-proper-list-nonempty-expression
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBProperList PBNonemptyExpression PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonempty-expression-none
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNonemptyExpression PBNone PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonempty-expression-nonvar
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNonemptyExpression PBNonvar PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonempty-expression-proper-list
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNonemptyExpression PBProperList PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonempty-expression-nonempty-expression
      (nik-rule-frame-v1
        (native-projection native-boundary-join)
        (role calculus))
      (head (PBBoundaryJoin PBNonemptyExpression PBNonemptyExpression PBNonemptyExpression))
      (body))
    (rule petta-boundary-consumer-car-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBCarAtom PFirst PBNonemptyExpression))
      (body))
    (rule petta-boundary-consumer-cdr-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBCdrAtom PFirst PBNonemptyExpression))
      (body))
    (rule petta-boundary-consumer-and-left
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBAnd PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-and-right
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBAnd PSecond PBNonvar))
      (body))
    (rule petta-boundary-consumer-or-left
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBOr PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-or-right
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBOr PSecond PBNonvar))
      (body))
    (rule petta-boundary-consumer-not
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBNot PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-xor-left
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBXor PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-xor-right
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBXor PSecond PBNonvar))
      (body))
    (rule petta-boundary-consumer-implies-left
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBImplies PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-implies-right
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBImplies PSecond PBNonvar))
      (body))
    (rule petta-boundary-consumer-reverse
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBReverse PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-last
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBLast PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-min-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBMinAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-max-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBMaxAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-size-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBSizeAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-length
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBLength PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-alpha-unique-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBAlphaUniqueAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-union-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBUnionAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-append
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBAppend PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-subtraction-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBSubtractionAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-intersection-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBIntersectionAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-map-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBMapAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-foldl-atom
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBFoldlAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-exclude-item
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBExcludeItem PSecond PBProperList))
      (body))
    (rule petta-boundary-consumer-map-flat
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBMapFlat PSecond PBProperList))
      (body))
    (rule petta-boundary-consumer-map-nested
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBMapNested PSecond PBProperList))
      (body))
    (rule petta-boundary-consumer-fold-flat
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBFoldFlat PThird PBProperList))
      (body))
    (rule petta-boundary-consumer-foldr-flat
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBFoldrFlat PThird PBProperList))
      (body))
    (rule petta-boundary-consumer-fold-nested
      (nik-rule-frame-v1
        (native-projection native-consumer-requirement)
        (role calculus))
      (head (PBConsumerRequirement PBFoldNested PThird PBProperList))
      (body))
    (rule petta-boundary-guard-is-expression-nonvar
      (nik-rule-frame-v1
        (native-projection native-guard-proviso)
        (role calculus))
      (head (PBGuardProvides PBIsExpressionTrue PBNonvar))
      (body))
    (rule petta-boundary-guard-is-expression-proper-list
      (nik-rule-frame-v1
        (native-projection native-guard-proviso)
        (role calculus))
      (head (PBGuardProvides PBIsExpressionTrue PBProperList))
      (body))
    (rule petta-boundary-guard-not-empty-nonvar
      (nik-rule-frame-v1
        (native-projection native-guard-proviso)
        (role calculus))
      (head (PBGuardProvides PBNotEmptyTrue PBNonvar))
      (body))
    (rule petta-boundary-guard-not-empty-proper-list
      (nik-rule-frame-v1
        (native-projection native-guard-proviso)
        (role calculus))
      (head (PBGuardProvides PBNotEmptyTrue PBProperList))
      (body))
    (rule petta-boundary-guard-not-empty-nonempty
      (nik-rule-frame-v1
        (native-projection native-guard-proviso)
        (role calculus))
      (head (PBGuardProvides PBNotEmptyTrue PBNonemptyExpression))
      (body))
    (rule petta-boundary-guard-cons-match-nonvar
      (nik-rule-frame-v1
        (native-projection native-guard-proviso)
        (role calculus))
      (head (PBGuardProvides PBConsMatchTrue PBNonvar))
      (body))
    (rule petta-boundary-guard-cons-match-nonempty
      (nik-rule-frame-v1
        (native-projection native-guard-proviso)
        (role calculus))
      (head (PBGuardProvides PBConsMatchTrue PBNonemptyExpression))
      (body))
    (rule petta-boundary-guard-not-variable
      (nik-rule-frame-v1
        (native-projection native-guard-proviso)
        (role calculus))
      (head (PBGuardProvides PBIsVariableFalse PBNonvar))
      (body))
    (rule petta-boundary-committed-none
      (nik-rule-frame-v1
        (native-projection native-boundary-visibility)
        (role calculus))
      (head (PBBoundaryVisible PBCommitted PBNone))
      (body))
    (rule petta-boundary-committed-nonvar
      (nik-rule-frame-v1
        (native-projection native-boundary-visibility)
        (role calculus))
      (head (PBBoundaryVisible PBCommitted PBNonvar))
      (body))
    (rule petta-boundary-committed-proper-list
      (nik-rule-frame-v1
        (native-projection native-boundary-visibility)
        (role calculus))
      (head (PBBoundaryVisible PBCommitted PBProperList))
      (body))
    (rule petta-boundary-committed-nonempty-expression
      (nik-rule-frame-v1
        (native-projection native-boundary-visibility)
        (role calculus))
      (head (PBBoundaryVisible PBCommitted PBNonemptyExpression))
      (body))
    (rule petta-boundary-open-none
      (nik-rule-frame-v1
        (native-projection native-boundary-visibility)
        (role calculus))
      (head (PBBoundaryVisible PBOpen PBNone))
      (body))
    (rule petta-boundary-open-nonempty-expression
      (nik-rule-frame-v1
        (native-projection native-boundary-visibility)
        (role calculus))
      (head (PBBoundaryVisible PBOpen PBNonemptyExpression))
      (body))
  ))
