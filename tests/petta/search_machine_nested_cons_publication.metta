(= (nested-cons:neutral-value)
   (quote
     (cons
       (node NestedConsItemV1
         (pair (cons (cp 7) nil) (some NestedConsPayloadV1)))
       (cons NestedConsSecondItemV1 nil))))

(= (nested-cons:neutral-ast)
   (let $statements (nested-cons:neutral-value)
     (quote (node mm0-file $statements))))

(= (gslt:mode:NestedConsSourceEnvironmentV1:1
      (gslt:source-value-v1 (node mm0-file $statements)))
   (quote
     (gslt:result:NestedConsSourceEnvironmentV1:1 $statements)))

(= (gslt:mode:NestedConsThreeInputsV1:111
      (gslt:source-value-v1 (cons $head $tail))
      (gslt:source-value-v1 $position)
      (gslt:source-value-v1 (NestedConsStateV1 $payload)))
   (quote
     (gslt:result:NestedConsThreeInputsV1:111
       $head $tail $position $payload)))

(= (nested-cons:raw-fold (cons $head $tail))
   (quote (NestedConsRawV1 $head $tail)))

(= (nested-cons:let-fold $input)
   (let
     (cons $head $tail) $input
     (quote (NestedConsLetV1 $head $tail))))

(= (nested-cons:let-choice $input)
   (let NestedConsChoiceLeftV1 $input
     (quote NestedConsChoseLeftV1)))

(= (nested-cons:let-choice $input)
   (let NestedConsChoiceRightV1 $input
     (quote NestedConsChoseRightV1)))

(= (gslt:mode:NestedConsStepV1:10 (cons $head $tail))
   (quote
     (gslt:result:NestedConsStepV1:10
       (NestedConsStepAcceptedV1
         (NestedConsStateV1 (cons $head $tail))))))

(= (gslt:mode:NestedConsAfterV1:10
      (NestedConsStepAcceptedV1 $state))
   (quote
     (gslt:result:NestedConsAfterV1:10
       (NestedConsDoneV1 $state))))

(= (gslt:mode:NestedConsFoldV1:10 (cons $head $tail))
   (let
     (gslt:result:NestedConsStepV1:10 $step-result)
     (gslt:mode:NestedConsStepV1:10 (cons $head $tail))
     (let
       (gslt:result:NestedConsAfterV1:10 $after-result)
       (gslt:mode:NestedConsAfterV1:10 $step-result)
       (quote
         (gslt:result:NestedConsFoldV1:10 $after-result)))))

(= (nested-cons:quoted-fold (quote (cons $head $tail)))
   (quote (NestedConsQuotedV1 $head $tail)))

(= (nested-cons:wrapped-fold
      (NestedConsValueV1 (cons $head $tail)))
   (quote (NestedConsWrappedV1 $head $tail)))

(= (gslt:mode:NestedConsWrappedRecursiveV1:1
      (NestedConsValueV1 nil))
   (quote
     (gslt:result:NestedConsWrappedRecursiveV1:1
       NestedConsWrappedNilV1)))

(= (gslt:mode:NestedConsWrappedRecursiveV1:1
      (NestedConsValueV1 (cons $head $tail)))
   (let
     (gslt:result:NestedConsWrappedRecursiveV1:1 $result)
     (let $answers
       (collapse
         (gslt:mode:NestedConsWrappedRecursiveV1:1
           (NestedConsValueV1 $tail)))
       (superpose $answers))
     (quote
       (gslt:result:NestedConsWrappedRecursiveV1:1
         (NestedConsWrappedConsV1 $head $result)))))

(: nested-cons:expression-fold (-> Expression %Undefined%))

(= (nested-cons:expression-fold (cons $head $tail))
   (quote (NestedConsExpressionV1 $head $tail)))

!(pair NestedConsRawQueryV1
   (let $neutral (nested-cons:neutral-value)
     (nested-cons:raw-fold $neutral)))

!(pair NestedConsQuotedQueryV1
   (let $neutral (nested-cons:neutral-value)
     (nested-cons:quoted-fold (quote $neutral))))

!(pair NestedConsWrappedQueryV1
   (let $neutral (nested-cons:neutral-value)
     (nested-cons:wrapped-fold (NestedConsValueV1 $neutral))))

!(pair NestedConsExpressionQueryV1
   (let $neutral (nested-cons:neutral-value)
     (nested-cons:expression-fold $neutral)))

!(pair NestedConsRawCollapseQueryV1
   (let $neutral (nested-cons:neutral-value)
     (collapse (nested-cons:raw-fold $neutral))))

!(pair NestedConsLetCollapseQueryV1
   (let $neutral (nested-cons:neutral-value)
     (collapse (nested-cons:let-fold $neutral))))

!(pair NestedConsLetChoiceQueryV1
   (collapse (nested-cons:let-choice NestedConsChoiceRightV1)))

!(pair NestedConsGeneratedModeCollapseQueryV1
   (let $neutral (nested-cons:neutral-value)
     (collapse (gslt:mode:NestedConsFoldV1:10 $neutral))))

!(pair NestedConsWrappedRecursiveQueryV1
   (let $neutral (nested-cons:neutral-value)
     (collapse
       (gslt:mode:NestedConsWrappedRecursiveV1:1
         (NestedConsValueV1 $neutral)))))

!(pair NestedConsSourceEnvironmentQueryV1
   (let $ast (nested-cons:neutral-ast)
     (collapse
       (gslt:mode:NestedConsSourceEnvironmentV1:1
         (gslt:source-value-v1 $ast)))))

!(pair NestedConsThreeInputsQueryV1
   (let $neutral (nested-cons:neutral-value)
     (collapse
       (gslt:mode:NestedConsThreeInputsV1:111
         (gslt:source-value-v1 $neutral)
         (gslt:source-value-v1 NatZeroV1)
         (gslt:source-value-v1
           (NestedConsStateV1 NestedConsPayloadV1))))))

!(pair NestedConsWrappedCollapseQueryV1
   (let $neutral (nested-cons:neutral-value)
     (collapse
       (nested-cons:wrapped-fold (NestedConsValueV1 $neutral)))))

!(pair NestedConsExpressionCollapseQueryV1
   (let $neutral (nested-cons:neutral-value)
     (collapse (nested-cons:expression-fold $neutral))))
