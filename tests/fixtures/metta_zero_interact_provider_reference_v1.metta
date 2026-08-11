; Finite relational provider used only by the authored interaction-semantics
; mutation gate.  Physical native-C and Rust/PathMap providers are exercised
; independently by the runtime provider gate.
(gslt-presentation-v1 MettaZeroInteractProviderReferenceV1
  (signature
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-succ 1)
    (operator q-sym 1)
    (operator q-var 1)
    (operator qabt-field-depth 4)
    (operator qabt-reference-nonbinding-head 1)
    (operator qabt-reference-zero-position 2)
    (operator qabt-transport 4)
    (operator zero-ground-apply 3)
    (operator zero-space-candidate 4)
    (operator zero-space-emit 6)
    (operator zero-space-open 3))
  (equations)
  (rewrites
    (rule qabt-reference-zero-position-here
      (head
        (qabt-reference-zero-position
          (q-cons ?head ?tail) q-zero))
      (body))
    (rule qabt-reference-zero-position-next
      (head
        (qabt-reference-zero-position
          (q-cons ?head ?tail) (q-succ ?position)))
      (body
        (qabt-reference-zero-position ?tail ?position)))
    (rule qabt-reference-head-fact
      (head (qabt-reference-nonbinding-head (q-sym (q-str "fact"))))
      (body))
    (rule qabt-reference-head-seen
      (head (qabt-reference-nonbinding-head (q-sym (q-str "seen"))))
      (body))
    (rule qabt-reference-head-equality
      (head (qabt-reference-nonbinding-head (q-sym (q-str "="))))
      (body))
    (rule qabt-reference-head-f
      (head (qabt-reference-nonbinding-head (q-sym (q-str "f"))))
      (body))
    (rule qabt-reference-head-g
      (head (qabt-reference-nonbinding-head (q-sym (q-str "g"))))
      (body))
    (rule qabt-reference-head-quote
      (head (qabt-reference-nonbinding-head (q-sym (q-str "quote"))))
      (body))
    (rule qabt-reference-field-depth-zero
      (head
        (qabt-field-depth ?head ?arguments ?position q-zero))
      (body
        (qabt-reference-nonbinding-head ?head)
        (qabt-reference-zero-position ?arguments ?position)))
    (rule qabt-reference-transport-same
      (head (qabt-transport ?depth ?depth ?value ?value))
      (body))
    (rule zero-interact-provider-reference-open
      (head
        (zero-space-open ?program world-0 test-open-receipt))
      (body))
    (rule zero-interact-provider-reference-fact-candidate
      (head
        (zero-space-candidate
          world-0
          (q-app
            (q-sym (q-str "fact"))
            (q-cons (q-var q-zero) q-nil))
          fact-0
          (q-app
            (q-sym (q-str "fact"))
            (q-cons (q-sym (q-str "a")) q-nil))))
      (body))
    (rule zero-interact-provider-reference-equation-candidate
      (head
        (zero-space-candidate
          world-0
          (q-app
            (q-sym (q-str "="))
            (q-cons
              (q-var q-zero)
              (q-cons (q-var (q-succ q-zero)) q-nil)))
          equation-0
          (q-app
            (q-sym (q-str "="))
            (q-cons
              (q-app
                (q-sym (q-str "f"))
                (q-cons (q-var q-zero) q-nil))
              (q-cons
                (q-app
                  (q-sym (q-str "g"))
                  (q-cons (q-var q-zero) q-nil))
                q-nil)))))
      (body))
    (rule zero-interact-provider-reference-ground
      (head
        (zero-ground-apply
          (q-sym (q-str "native"))
          zero-ground-proof
          (q-sym (q-str "grounded"))))
      (body))
    (rule zero-interact-provider-reference-emit
      (head
        (zero-space-emit
          world-0 ?event ?atom world-1
          emitted-0 test-emit-receipt))
      (body))
  ))
