; ZeroUV keeps the query-derived one-step relation untotalized and makes
; repeated control explicit.  The syntax experiments are:
;
;   (! (step subject))
;   (! (reach fuel subject))
;   (! (seek fuel pattern subject))
;   (! (follow policy memory fuel subject))
;   (! (recur policy path accepting-state))
;
; Fuel uses z and (s fuel).  Paths are proof-relevant nested values:
; (path state rest), ending in end.  Controller policies are ordinary
; equations.  A memoryful choice has the form
; (= (policy memory state) (choose next-memory next-state)); a memoryless
; recurrence policy uses (= (policy state) next-state).
(gslt-presentation-v1 MettaZeroUVControlV1
  (signature
    (operator gslt-produced 2)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1)
    (operator qmatch-term 4)
    (operator zero-evaluate-request 2)
    (operator zero-produce 4)
    (operator zero-program-atom 3)
    (operator zerouv-accept-evidence 2)
    (operator zerouv-choice-evidence 3)
    (operator zerouv-classify 3)
    (operator zerouv-cycle-base-evidence 1)
    (operator zerouv-cycle-evidence 3)
    (operator zerouv-cycle-request 4)
    (operator zerouv-cycle-valid 7)
    (operator zerouv-edge-evidence 2)
    (operator zerouv-follow-base-evidence 2)
    (operator zerouv-follow-evidence 3)
    (operator zerouv-follow-request 6)
    (operator zerouv-follow-result-evidence 2)
    (operator zerouv-follow-valid 9)
    (operator zerouv-observe 4)
    (operator zerouv-observed-evidence 1)
    (operator zerouv-path-member 3)
    (operator zerouv-path-member-here-evidence 1)
    (operator zerouv-path-member-rest-evidence 2)
    (operator zerouv-policy-edge 7)
    (operator zerouv-produce 4)
    (operator zerouv-produced-cons 2)
    (operator zerouv-produced-member 2)
    (operator zerouv-reach-base-evidence 1)
    (operator zerouv-reach-evidence 3)
    (operator zerouv-reach-request 4)
    (operator zerouv-reach-valid 7)
    (operator zerouv-result-request 1)
    (operator zerouv-seek-hit-evidence 2)
    (operator zerouv-seek-request 5)
    (operator zerouv-seek-step-evidence 2)
    (operator zerouv-seek-valid 8)
    (operator zerouv-step-completed-evidence 1)
    (operator zerouv-step-next-evidence 2)
    (operator zerouv-step-request 2))
  (equations)
  (rewrites
    ; Syntax classification is intentionally specific.  Unknown commands do
    ; not fall through to evaluation as an accidental second language.
    (rule zerouv-classify-step
      (head
        (zerouv-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons
              (q-app
                (q-sym (q-str "step"))
                (q-cons ?subject q-nil))
              q-nil))
          (zerouv-step-request ?request-occurrence ?subject)))
      (body))

    (rule zerouv-classify-reach
      (head
        (zerouv-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons
              (q-app
                (q-sym (q-str "reach"))
                (q-cons ?fuel (q-cons ?subject q-nil)))
              q-nil))
          (zerouv-reach-request
            ?request-occurrence ?fuel ?subject ?subject)))
      (body))

    (rule zerouv-classify-seek
      (head
        (zerouv-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons
              (q-app
                (q-sym (q-str "seek"))
                (q-cons ?fuel
                  (q-cons ?pattern (q-cons ?subject q-nil))))
              q-nil))
          (zerouv-seek-request
            ?request-occurrence ?fuel ?pattern ?subject ?subject)))
      (body))

    (rule zerouv-classify-follow
      (head
        (zerouv-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons
              (q-app
                (q-sym (q-str "follow"))
                (q-cons ?policy
                  (q-cons ?memory
                    (q-cons ?fuel (q-cons ?subject q-nil)))))
              q-nil))
          (zerouv-follow-request
            ?request-occurrence ?policy ?memory ?fuel ?subject ?subject)))
      (body))

    (rule zerouv-classify-recur
      (head
        (zerouv-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons
              (q-app
                (q-sym (q-str "recur"))
                (q-cons ?policy
                  (q-cons ?path (q-cons ?accepting q-nil))))
              q-nil))
          (zerouv-cycle-request
            ?request-occurrence ?policy ?path ?accepting)))
      (body))

    ; One exact step delegates to Zero's public-query-derived producer.
    (rule zerouv-produce-step
      (head
        (zerouv-produce
          ?program
          (zerouv-step-request ?request-occurrence ?subject)
          ?evidence ?result))
      (body
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          ?evidence ?result)))

    ; Exact bounded reachability.  Every recursive edge is a genuine kernel
    ; result occurrence; no completion is claimed at the fuel boundary.
    (rule zerouv-reach-zero
      (head
        (zerouv-reach-valid
          ?program ?request-occurrence
          (q-sym (q-str "z")) ?subject
          (zerouv-reach-base-evidence ?request-occurrence)
          ?subject
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?subject (q-cons (q-sym (q-str "end")) q-nil)))))
      (body))

    (rule zerouv-reach-successor
      (head
        (zerouv-reach-valid
          ?program ?request-occurrence
          (q-app
            (q-sym (q-str "s"))
            (q-cons ?fuel q-nil))
          ?subject
          (zerouv-reach-evidence
            ?request-occurrence ?edge-evidence ?rest-evidence)
          ?result
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?subject (q-cons ?rest-path q-nil)))))
      (body
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          ?edge-evidence ?next)
        (zerouv-reach-valid
          ?program ?request-occurrence ?fuel ?next
          ?rest-evidence ?result ?rest-path)))

    (rule zerouv-produce-reach
      (head
        (zerouv-produce
          ?program
          (zerouv-reach-request
            ?request-occurrence ?fuel ?root ?subject)
          ?evidence
          (q-app
            (q-sym (q-str "reachable"))
            (q-cons ?result (q-cons ?path q-nil)))))
      (body
        (zerouv-reach-valid
          ?program ?request-occurrence ?fuel ?subject
          ?evidence ?result ?path)))

    ; Seek is a least-fixed-point experiment: a finite matching path is its
    ; proof object.  The hit may occur before all supplied fuel is consumed.
    (rule zerouv-seek-hit
      (head
        (zerouv-seek-valid
          ?program ?request-occurrence ?fuel ?pattern ?subject
          (zerouv-seek-hit-evidence ?request-occurrence ?environment)
          ?subject
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?subject (q-cons (q-sym (q-str "end")) q-nil)))))
      (body
        (qmatch-term
          ?pattern ?subject qmatch-env-nil ?environment)))

    (rule zerouv-seek-step
      (head
        (zerouv-seek-valid
          ?program ?request-occurrence
          (q-app
            (q-sym (q-str "s"))
            (q-cons ?fuel q-nil))
          ?pattern ?subject
          (zerouv-seek-step-evidence ?edge-evidence ?rest-evidence)
          ?result
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?subject (q-cons ?rest-path q-nil)))))
      (body
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          ?edge-evidence ?next)
        (zerouv-seek-valid
          ?program ?request-occurrence ?fuel ?pattern ?next
          ?rest-evidence ?result ?rest-path)))

    (rule zerouv-produce-seek
      (head
        (zerouv-produce
          ?program
          (zerouv-seek-request
            ?request-occurrence ?fuel ?pattern ?root ?subject)
          ?evidence
          (q-app
            (q-sym (q-str "found"))
            (q-cons ?result (q-cons ?path q-nil)))))
      (body
        (zerouv-seek-valid
          ?program ?request-occurrence ?fuel ?pattern ?subject
          ?evidence ?result ?path)))

    ; A memoryful controller is an ordinary equation whose result is a
    ; `(choose next-memory next-state)` value.  The chosen state is accepted
    ; only when it is also an actual kernel successor.
    (rule zerouv-policy-edge-memoryful
      (head
        (zerouv-policy-edge
          ?program ?request-occurrence ?policy ?memory ?subject
          (zerouv-choice-evidence ?choice-evidence ?edge-evidence ?next-memory)
          (q-app
            (q-sym (q-str "choice"))
            (q-cons ?next-memory (q-cons ?next q-nil)))))
      (body
        (zero-produce
          ?program
          (zero-evaluate-request
            ?request-occurrence
            (q-app ?policy
              (q-cons ?memory (q-cons ?subject q-nil))))
          ?choice-evidence
          (q-app
            (q-sym (q-str "choose"))
            (q-cons ?next-memory (q-cons ?next q-nil))))
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          ?edge-evidence ?next)))

    (rule zerouv-follow-zero
      (head
        (zerouv-follow-valid
          ?program ?request-occurrence ?policy ?memory
          (q-sym (q-str "z")) ?subject
          (zerouv-follow-base-evidence ?request-occurrence ?memory)
          ?subject
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?subject (q-cons (q-sym (q-str "end")) q-nil)))))
      (body))

    (rule zerouv-follow-successor
      (head
        (zerouv-follow-valid
          ?program ?request-occurrence ?policy ?memory
          (q-app
            (q-sym (q-str "s"))
            (q-cons ?fuel q-nil))
          ?subject
          (zerouv-follow-evidence
            ?choice-evidence ?rest-evidence ?next-memory)
          ?result
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?subject (q-cons ?rest-path q-nil)))))
      (body
        (zerouv-policy-edge
          ?program ?request-occurrence ?policy ?memory ?subject
          ?choice-evidence
          (q-app
            (q-sym (q-str "choice"))
            (q-cons ?next-memory (q-cons ?next q-nil))))
        (zerouv-follow-valid
          ?program ?request-occurrence ?policy ?next-memory ?fuel ?next
          ?rest-evidence ?result ?rest-path)))

    (rule zerouv-produce-follow
      (head
        (zerouv-produce
          ?program
          (zerouv-follow-request
            ?request-occurrence ?policy ?memory ?fuel ?root ?subject)
          (zerouv-follow-result-evidence ?memory ?evidence)
          (q-app
            (q-sym (q-str "residual"))
            (q-cons ?result (q-cons ?path q-nil)))))
      (body
        (zerouv-follow-valid
          ?program ?request-occurrence ?policy ?memory ?fuel ?subject
          ?evidence ?result ?path)))

    ; Memoryless policy edge for a finite lasso certificate.
    (rule zerouv-policy-edge-memoryless
      (head
        (zerouv-policy-edge
          ?program ?request-occurrence ?policy
          (q-sym (q-str "memoryless")) ?subject
          (zerouv-edge-evidence ?choice-evidence ?edge-evidence)
          (q-app
            (q-sym (q-str "choice"))
            (q-cons
              (q-sym (q-str "memoryless"))
              (q-cons ?next q-nil)))))
      (body
        (zero-produce
          ?program
          (zero-evaluate-request
            ?request-occurrence
            (q-app ?policy (q-cons ?subject q-nil)))
          ?choice-evidence ?next)
        (zero-produce
          ?program
          (zero-evaluate-request ?request-occurrence ?subject)
          ?edge-evidence ?next)))

    ; A path is valid when each adjacent pair follows the selected policy and
    ; its final state is the root.  This is a deterministic lasso witness.
    (rule zerouv-cycle-valid-step
      (head
        (zerouv-cycle-valid
          ?program ?request-occurrence ?policy ?root ?current
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?current
              (q-cons
                (q-app
                  (q-sym (q-str "path"))
                  (q-cons ?next (q-cons ?rest q-nil)))
                q-nil)))
          (zerouv-cycle-evidence
            ?edge-evidence ?rest-evidence ?current)))
      (body
        (zerouv-policy-edge
          ?program ?request-occurrence ?policy
          (q-sym (q-str "memoryless")) ?current
          ?edge-evidence
          (q-app
            (q-sym (q-str "choice"))
            (q-cons
              (q-sym (q-str "memoryless"))
              (q-cons ?next q-nil))))
        (zerouv-cycle-valid
          ?program ?request-occurrence ?policy ?root ?next
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?next (q-cons ?rest q-nil)))
          ?rest-evidence)))

    (rule zerouv-cycle-valid-close
      (head
        (zerouv-cycle-valid
          ?program ?request-occurrence ?policy ?root ?root
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?root (q-cons (q-sym (q-str "end")) q-nil)))
          (zerouv-cycle-base-evidence ?root)))
      (body))

    (rule zerouv-path-member-here
      (head
        (zerouv-path-member
          ?state
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?state (q-cons ?rest q-nil)))
          (zerouv-path-member-here-evidence ?state)))
      (body))

    (rule zerouv-path-member-rest
      (head
        (zerouv-path-member
          ?state
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?head (q-cons ?rest q-nil)))
          (zerouv-path-member-rest-evidence ?head ?evidence)))
      (body (zerouv-path-member ?state ?rest ?evidence)))

    (rule zerouv-produce-cycle
      (head
        (zerouv-produce
          ?program
          (zerouv-cycle-request
            ?request-occurrence ?policy
            (q-app
              (q-sym (q-str "path"))
              (q-cons ?root (q-cons ?rest q-nil)))
            ?accepting)
          (zerouv-accept-evidence ?cycle-evidence ?member-evidence)
          (q-app
            (q-sym (q-str "recurrent"))
            (q-cons ?policy
              (q-cons ?root
                (q-cons ?accepting
                  (q-cons
                    (q-app
                      (q-sym (q-str "path"))
                      (q-cons ?root (q-cons ?rest q-nil)))
                    q-nil)))))))
      (body
        (zerouv-cycle-valid
          ?program ?request-occurrence ?policy ?root ?root
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?root (q-cons ?rest q-nil)))
          ?cycle-evidence)
        (zerouv-path-member
          ?accepting
          (q-app
            (q-sym (q-str "path"))
            (q-cons ?root (q-cons ?rest q-nil)))
          ?member-evidence)
        (zero-program-atom
          ?program ?accept-occurrence
          (q-app
            (q-sym (q-str "accept"))
            (q-cons ?policy (q-cons ?accepting q-nil))))))

    ; Produced-bag observation.  Step is the only command whose exact empty
    ; bag has semantic content: it denotes kernel normal form.
    (rule zerouv-produced-member-here
      (head
        (zerouv-produced-member
          (zerouv-produced-cons ?item ?rest) ?item))
      (body))

    (rule zerouv-produced-member-rest
      (head
        (zerouv-produced-member
          (zerouv-produced-cons ?head ?rest) ?item))
      (body (zerouv-produced-member ?rest ?item)))

    ; Only non-step commands expose their produced result directly.  A step
    ; has a distinct `next`/`done` observation and must not also leak a raw
    ; result through the generic bag observer.
    (rule zerouv-result-request-reach
      (head
        (zerouv-result-request
          (zerouv-reach-request
            ?request-occurrence ?fuel ?root ?subject)))
      (body))

    (rule zerouv-result-request-seek
      (head
        (zerouv-result-request
          (zerouv-seek-request
            ?request-occurrence ?fuel ?pattern ?root ?subject)))
      (body))

    (rule zerouv-result-request-follow
      (head
        (zerouv-result-request
          (zerouv-follow-request
            ?request-occurrence ?policy ?memory ?fuel ?root ?subject)))
      (body))

    (rule zerouv-result-request-cycle
      (head
        (zerouv-result-request
          (zerouv-cycle-request
            ?request-occurrence ?policy ?path ?accepting)))
      (body))

    (rule zerouv-observe-step-next
      (head
        (zerouv-observe
          (zerouv-step-request ?request-occurrence ?subject)
          ?bag
          (zerouv-step-next-evidence
            ?request-occurrence ?producer-evidence)
          (q-app
            (q-sym (q-str "next"))
            (q-cons ?result q-nil))))
      (body
        (zerouv-produced-member
          ?bag (gslt-produced ?producer-evidence ?result))))

    (rule zerouv-observe-step-completed
      (head
        (zerouv-observe
          (zerouv-step-request ?request-occurrence ?subject)
          zerouv-produced-nil
          (zerouv-step-completed-evidence ?request-occurrence)
          (q-app
            (q-sym (q-str "done"))
            (q-cons ?subject q-nil))))
      (body))

    (rule zerouv-observe-produced
      (head
        (zerouv-observe
          ?request ?bag
          (zerouv-observed-evidence ?producer-evidence)
          ?result))
      (body
        (zerouv-result-request ?request)
        (zerouv-produced-member
          ?bag (gslt-produced ?producer-evidence ?result))))
  ))
