; Compile reflected policy occurrences, not the set of policy answers.
; The consumer must require every source occurrence to be admitted and exactly
; one occurrence per input cell.  Equal answers from different rules remain
; distinct here.  Matching shares one environment across all five arguments.
(gslt-presentation-v1 MatchDecisionPolicyTableV1
  (signature
    (operator md-compile 1)
    (operator md-source 1)
    (operator md-admitted 1)
    (operator md-cell 6)
    (operator md-rule 1)
    (operator md-domain 2)
    (operator md-pattern 2)
    (operator md-index 1))
  (equations)
  (rewrites
    (rule md-observation-unknown
      (head (md-domain observation unknown)) (body))
    (rule md-observation-absent
      (head (md-domain observation absent)) (body))
    (rule md-observation-literal
      (head (md-domain observation literal)) (body))
    (rule md-observation-expression
      (head (md-domain observation expression)) (body))
    (rule md-key-wildcard
      (head (md-domain key wildcard)) (body))
    (rule md-key-literal
      (head (md-domain key literal)) (body))
    (rule md-key-arity
      (head (md-domain key expression-arity)) (body))
    (rule md-key-head
      (head (md-domain key expression-head)) (body))
    (rule md-relation-different
      (head (md-domain relation different)) (body))
    (rule md-relation-equal
      (head (md-domain relation equal)) (body))
    (rule md-outcome-fallback
      (head (md-domain outcome fallback)) (body))
    (rule md-outcome-keep
      (head (md-domain outcome keep)) (body))
    (rule md-outcome-refute
      (head (md-domain outcome refute)) (body))

    (rule md-index-zero
      (head (md-index q-zero)) (body))
    (rule md-index-successor
      (head (md-index (q-succ ?index))) (body (md-index ?index)))
    (rule md-pattern-variable
      (head (md-pattern ?domain (q-var ?index)))
      (body (md-index ?index)))
    (rule md-pattern-constant
      (head (md-pattern ?domain (q-sym ?value)))
      (body (md-domain ?domain ?value)))

    (rule md-admit-fact
      (head
        (md-rule
          (q-rule ?name
            (q-app (q-sym candidate-policy)
              (q-cons ?observation (q-cons ?key (q-cons ?arity
                (q-cons ?identity (q-cons (q-sym ?outcome) q-nil))))))
            q-nil)))
      (body
        (md-pattern observation ?observation)
        (md-pattern key ?key)
        (md-pattern relation ?arity)
        (md-pattern relation ?identity)
        (md-domain outcome ?outcome)))

    (rule md-emit-source
      (head (md-compile (md-source ?rule)))
      (body (source-rule (q-sym MatchDecisionPolicyV1) ?rule)))
    (rule md-emit-admitted
      (head (md-compile (md-admitted ?rule)))
      (body
        (source-rule (q-sym MatchDecisionPolicyV1) ?rule)
        (md-rule ?rule)))
    (rule md-emit-cell
      (head
        (md-compile
          (md-cell (q-rule ?name ?head q-nil)
            ?observation ?key ?arity ?identity ?outcome)))
      (body
        (source-rule (q-sym MatchDecisionPolicyV1)
          (q-rule ?name ?head q-nil))
        (md-rule (q-rule ?name ?head q-nil))
        (md-domain observation ?observation)
        (md-domain key ?key)
        (md-domain relation ?arity)
        (md-domain relation ?identity)
        (md-domain outcome ?outcome)
        (qmatch-term ?head
          (q-app (q-sym candidate-policy)
            (q-cons (q-sym ?observation) (q-cons (q-sym ?key)
              (q-cons (q-sym ?arity) (q-cons (q-sym ?identity)
                (q-cons (q-sym ?outcome) q-nil))))))
          qmatch-env-nil ?environment)))
  ))
