(gslt-presentation-v1 petta-typecheck-v2-boundary-core-v1
  (signature
    (operator PBBoundaryJoin 3)
    (operator PBConsumerRequirement 3)
    (operator PBGuardProvides 2)
    (operator PBBoundaryVisible 2)
  )
  (equations)
  (rewrites
    (rule petta-boundary-join-none-none
      (head (PBBoundaryJoin PBNone PBNone PBNone))
      (body))
    (rule petta-boundary-join-none-nonvar
      (head (PBBoundaryJoin PBNone PBNonvar PBNonvar))
      (body))
    (rule petta-boundary-join-none-proper-list
      (head (PBBoundaryJoin PBNone PBProperList PBProperList))
      (body))
    (rule petta-boundary-join-none-nonempty-expression
      (head (PBBoundaryJoin PBNone PBNonemptyExpression PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonvar-none
      (head (PBBoundaryJoin PBNonvar PBNone PBNonvar))
      (body))
    (rule petta-boundary-join-nonvar-nonvar
      (head (PBBoundaryJoin PBNonvar PBNonvar PBNonvar))
      (body))
    (rule petta-boundary-join-nonvar-proper-list
      (head (PBBoundaryJoin PBNonvar PBProperList PBProperList))
      (body))
    (rule petta-boundary-join-nonvar-nonempty-expression
      (head (PBBoundaryJoin PBNonvar PBNonemptyExpression PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-proper-list-none
      (head (PBBoundaryJoin PBProperList PBNone PBProperList))
      (body))
    (rule petta-boundary-join-proper-list-nonvar
      (head (PBBoundaryJoin PBProperList PBNonvar PBProperList))
      (body))
    (rule petta-boundary-join-proper-list-proper-list
      (head (PBBoundaryJoin PBProperList PBProperList PBProperList))
      (body))
    (rule petta-boundary-join-proper-list-nonempty-expression
      (head (PBBoundaryJoin PBProperList PBNonemptyExpression PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonempty-expression-none
      (head (PBBoundaryJoin PBNonemptyExpression PBNone PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonempty-expression-nonvar
      (head (PBBoundaryJoin PBNonemptyExpression PBNonvar PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonempty-expression-proper-list
      (head (PBBoundaryJoin PBNonemptyExpression PBProperList PBNonemptyExpression))
      (body))
    (rule petta-boundary-join-nonempty-expression-nonempty-expression
      (head (PBBoundaryJoin PBNonemptyExpression PBNonemptyExpression PBNonemptyExpression))
      (body))
    (rule petta-boundary-consumer-car-atom
      (head (PBConsumerRequirement PBCarAtom PFirst PBNonemptyExpression))
      (body))
    (rule petta-boundary-consumer-cdr-atom
      (head (PBConsumerRequirement PBCdrAtom PFirst PBNonemptyExpression))
      (body))
    (rule petta-boundary-consumer-and-left
      (head (PBConsumerRequirement PBAnd PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-and-right
      (head (PBConsumerRequirement PBAnd PSecond PBNonvar))
      (body))
    (rule petta-boundary-consumer-or-left
      (head (PBConsumerRequirement PBOr PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-or-right
      (head (PBConsumerRequirement PBOr PSecond PBNonvar))
      (body))
    (rule petta-boundary-consumer-not
      (head (PBConsumerRequirement PBNot PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-xor-left
      (head (PBConsumerRequirement PBXor PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-xor-right
      (head (PBConsumerRequirement PBXor PSecond PBNonvar))
      (body))
    (rule petta-boundary-consumer-implies-left
      (head (PBConsumerRequirement PBImplies PFirst PBNonvar))
      (body))
    (rule petta-boundary-consumer-implies-right
      (head (PBConsumerRequirement PBImplies PSecond PBNonvar))
      (body))
    (rule petta-boundary-consumer-reverse
      (head (PBConsumerRequirement PBReverse PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-last
      (head (PBConsumerRequirement PBLast PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-min-atom
      (head (PBConsumerRequirement PBMinAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-max-atom
      (head (PBConsumerRequirement PBMaxAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-size-atom
      (head (PBConsumerRequirement PBSizeAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-length
      (head (PBConsumerRequirement PBLength PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-alpha-unique-atom
      (head (PBConsumerRequirement PBAlphaUniqueAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-union-atom
      (head (PBConsumerRequirement PBUnionAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-append
      (head (PBConsumerRequirement PBAppend PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-subtraction-atom
      (head (PBConsumerRequirement PBSubtractionAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-intersection-atom
      (head (PBConsumerRequirement PBIntersectionAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-map-atom
      (head (PBConsumerRequirement PBMapAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-foldl-atom
      (head (PBConsumerRequirement PBFoldlAtom PFirst PBProperList))
      (body))
    (rule petta-boundary-consumer-exclude-item
      (head (PBConsumerRequirement PBExcludeItem PSecond PBProperList))
      (body))
    (rule petta-boundary-consumer-map-flat
      (head (PBConsumerRequirement PBMapFlat PSecond PBProperList))
      (body))
    (rule petta-boundary-consumer-map-nested
      (head (PBConsumerRequirement PBMapNested PSecond PBProperList))
      (body))
    (rule petta-boundary-consumer-fold-flat
      (head (PBConsumerRequirement PBFoldFlat PThird PBProperList))
      (body))
    (rule petta-boundary-consumer-foldr-flat
      (head (PBConsumerRequirement PBFoldrFlat PThird PBProperList))
      (body))
    (rule petta-boundary-consumer-fold-nested
      (head (PBConsumerRequirement PBFoldNested PThird PBProperList))
      (body))
    (rule petta-boundary-guard-is-expression-nonvar
      (head (PBGuardProvides PBIsExpressionTrue PBNonvar))
      (body))
    (rule petta-boundary-guard-is-expression-proper-list
      (head (PBGuardProvides PBIsExpressionTrue PBProperList))
      (body))
    (rule petta-boundary-guard-not-empty-nonvar
      (head (PBGuardProvides PBNotEmptyTrue PBNonvar))
      (body))
    (rule petta-boundary-guard-not-empty-proper-list
      (head (PBGuardProvides PBNotEmptyTrue PBProperList))
      (body))
    (rule petta-boundary-guard-not-empty-nonempty
      (head (PBGuardProvides PBNotEmptyTrue PBNonemptyExpression))
      (body))
    (rule petta-boundary-guard-cons-match-nonvar
      (head (PBGuardProvides PBConsMatchTrue PBNonvar))
      (body))
    (rule petta-boundary-guard-cons-match-nonempty
      (head (PBGuardProvides PBConsMatchTrue PBNonemptyExpression))
      (body))
    (rule petta-boundary-guard-not-variable
      (head (PBGuardProvides PBIsVariableFalse PBNonvar))
      (body))
    (rule petta-boundary-committed-none
      (head (PBBoundaryVisible PBCommitted PBNone))
      (body))
    (rule petta-boundary-committed-nonvar
      (head (PBBoundaryVisible PBCommitted PBNonvar))
      (body))
    (rule petta-boundary-committed-proper-list
      (head (PBBoundaryVisible PBCommitted PBProperList))
      (body))
    (rule petta-boundary-committed-nonempty-expression
      (head (PBBoundaryVisible PBCommitted PBNonemptyExpression))
      (body))
    (rule petta-boundary-open-none
      (head (PBBoundaryVisible PBOpen PBNone))
      (body))
    (rule petta-boundary-open-nonempty-expression
      (head (PBBoundaryVisible PBOpen PBNonemptyExpression))
      (body))
  ))
