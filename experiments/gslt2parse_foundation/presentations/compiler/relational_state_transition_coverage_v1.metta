; Derive the primitive action constructors emitted by the state-action
; lowering and the constructors interpreted by StateStepV1.  Both inventories
; come from quotations of the authored presentations; this file carries no
; handwritten action list.

(gslt-presentation-v1 RelationalStateTransitionCoverageV1
  (signature
    (operator ActionsConsV1 2)
    (operator LowerStateActionV1 3)
    (operator StateStepV1 7)
    (operator TransitionActionCoverageObservedV1 2)
    (operator TransitionQuotedActionsConstructorV1 2)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-rule 3)
    (operator q-sym 1)
    (operator source-rule 2))
  (equations)
  (rewrites
    (rule transition-quoted-actions-applied-constructor-head-v1
      (head
        (TransitionQuotedActionsConstructorV1
          (q-app (q-sym ActionsConsV1)
            (q-cons
              (q-app (q-sym ?constructor) ?arguments)
              (q-cons ?rest q-nil)))
          ?constructor))
      (body))
    (rule transition-quoted-actions-nullary-constructor-head-v1
      (head
        (TransitionQuotedActionsConstructorV1
          (q-app (q-sym ActionsConsV1)
            (q-cons (q-sym ?constructor) (q-cons ?rest q-nil)))
          ?constructor))
      (body))
    (rule transition-quoted-actions-constructor-tail-v1
      (head
        (TransitionQuotedActionsConstructorV1
          (q-app (q-sym ActionsConsV1)
            (q-cons ?action (q-cons ?rest q-nil)))
          ?constructor))
      (body
        (TransitionQuotedActionsConstructorV1 ?rest ?constructor)))
    (rule transition-action-coverage-lowered-v1
      (head
        (TransitionActionCoverageObservedV1
          transition-action-coverage-lowered-v1 ?constructor))
      (body
        (source-rule ?owner
          (q-rule ?name
            (q-app (q-sym LowerStateActionV1)
              (q-cons ?package
                (q-cons ?source-action
                  (q-cons ?actions q-nil))))
            ?body))
        (TransitionQuotedActionsConstructorV1 ?actions ?constructor)))
    (rule transition-action-coverage-stepped-applied-v1
      (head
        (TransitionActionCoverageObservedV1
          transition-action-coverage-stepped-v1 ?constructor))
      (body
        (source-rule ?owner
          (q-rule ?name
            (q-app (q-sym StateStepV1)
              (q-cons ?package
                (q-cons ?before
                  (q-cons ?occurrence
                    (q-cons
                      (q-app (q-sym ?constructor) ?arguments)
                      ?rest)))))
            ?body))))
    (rule transition-action-coverage-stepped-nullary-v1
      (head
        (TransitionActionCoverageObservedV1
          transition-action-coverage-stepped-v1 ?constructor))
      (body
        (source-rule ?owner
          (q-rule ?name
            (q-app (q-sym StateStepV1)
              (q-cons ?package
                (q-cons ?before
                  (q-cons ?occurrence
                    (q-cons (q-sym ?constructor) ?rest)))))
            ?body))))))
