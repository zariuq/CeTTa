; Finite executable waist for indexed GSLT commands.  Authors use named
; spaces, directed equations, route declarations, ordinary route application,
; and `!`.  The generated relations restore endpoints and occurrence evidence
; for the typed compilation IR.
(gslt-presentation-v1 GsltIlFiniteIndexedCommandV1
  (signature
    (operator gslt-il-classify 3)
    (operator gslt-il-observe 4)
    (operator gslt-il-observed-evidence 1)
    (operator gslt-il-produce 4)
    (operator gslt-il-produced-cons 2)
    (operator gslt-il-produced-member 2)
    (operator gslt-il-program-atom 3)
    (operator gslt-il-program-cons 3)
    (operator gslt-il-route-decl 5)
    (operator gslt-il-route-evidence 3)
    (operator gslt-il-route-row 8)
    (operator gslt-il-route-rule 5)
    (operator gslt-il-route-space-evidence 3)
    (operator gslt-il-space-evidence 2)
    (operator gslt-il-space-rule 5)
    (operator gslt-il-step-request 2)
    (operator gslt-produced 2)
    (operator q-app 2)
    (operator q-cons 2)
    (operator q-str 1)
    (operator q-sym 1))
  (equations)
  (rewrites
    (rule gslt-il-classify-step
      (head
        (gslt-il-classify
          ?request-occurrence
          (q-app
            (q-sym (q-str "!"))
            (q-cons ?command q-nil))
          (gslt-il-step-request ?request-occurrence ?command)))
      (body))

    ; The list is only an occurrence-preserving carrier.  No semantic rule
    ; observes its position.
    (rule gslt-il-program-atom-here
      (head
        (gslt-il-program-atom
          (gslt-il-program-cons ?occurrence ?atom ?rest)
          ?occurrence ?atom))
      (body))
    (rule gslt-il-program-atom-rest
      (head
        (gslt-il-program-atom
          (gslt-il-program-cons ?head-occurrence ?head-atom ?rest)
          ?occurrence ?atom))
      (body (gslt-il-program-atom ?rest ?occurrence ?atom)))

    (rule gslt-il-space-rule-from-atom
      (head
        (gslt-il-space-rule
          ?program ?rule-occurrence ?space ?source ?target))
      (body
        (gslt-il-program-atom
          ?program ?rule-occurrence
          (q-app
            (q-sym (q-str "in"))
            (q-cons ?space
              (q-cons
                (q-app
                  (q-sym (q-str "="))
                  (q-cons ?source (q-cons ?target q-nil)))
                q-nil))))))

    (rule gslt-il-route-decl-from-atom
      (head
        (gslt-il-route-decl
          ?program ?declaration-occurrence ?route
          ?source-space ?target-space))
      (body
        (gslt-il-program-atom
          ?program ?declaration-occurrence
          (q-app
            (q-sym (q-str "route"))
            (q-cons ?route
              (q-cons ?source-space (q-cons ?target-space q-nil)))))))

    (rule gslt-il-route-rule-from-atom
      (head
        (gslt-il-route-rule
          ?program ?rule-occurrence ?route ?source ?target))
      (body
        (gslt-il-program-atom
          ?program ?rule-occurrence
          (q-app
            (q-sym (q-str "="))
            (q-cons
              (q-app ?route (q-cons ?source q-nil))
              (q-cons ?target q-nil))))))

    ; Endpoints are authored once on the route declaration.  Joining that
    ; declaration with an ordinary route equation constructs the typed row.
    (rule gslt-il-route-row-from-declaration-and-rule
      (head
        (gslt-il-route-row
          ?program ?declaration-occurrence ?rule-occurrence ?route
          ?source-space ?target-space ?source ?target))
      (body
        (gslt-il-route-decl
          ?program ?declaration-occurrence ?route
          ?source-space ?target-space)
        (gslt-il-route-rule
          ?program ?rule-occurrence ?route ?source ?target)))

    (rule gslt-il-produce-in-space
      (head
        (gslt-il-produce
          ?program
          (gslt-il-step-request
            ?request-occurrence
            (q-app
              (q-sym (q-str "in"))
              (q-cons ?space (q-cons ?state q-nil))))
          (gslt-il-space-evidence ?request-occurrence ?rule-occurrence)
          (q-app
            (q-sym (q-str "in"))
            (q-cons ?space (q-cons ?next q-nil)))))
      (body
        (gslt-il-space-rule
          ?program ?rule-occurrence ?space ?state ?next)))

    (rule gslt-il-produce-route-under-space-step
      (head
        (gslt-il-produce
          ?program
          (gslt-il-step-request
            ?request-occurrence
            (q-app ?route (q-cons ?state q-nil)))
          (gslt-il-route-space-evidence
            ?request-occurrence ?declaration-occurrence ?rule-occurrence)
          (q-app ?route (q-cons ?next q-nil))))
      (body
        (gslt-il-route-decl
          ?program ?declaration-occurrence ?route
          ?source-space ?target-space)
        (gslt-il-space-rule
          ?program ?rule-occurrence ?source-space ?state ?next)))

    (rule gslt-il-produce-apply-route
      (head
        (gslt-il-produce
          ?program
          (gslt-il-step-request
            ?request-occurrence
            (q-app ?route (q-cons ?state q-nil)))
          (gslt-il-route-evidence
            ?request-occurrence ?declaration-occurrence ?rule-occurrence)
          (q-app
            (q-sym (q-str "in"))
            (q-cons ?target-space (q-cons ?transported q-nil)))))
      (body
        (gslt-il-route-row
          ?program ?declaration-occurrence ?rule-occurrence ?route
          ?source-space ?target-space ?state ?transported)))

    (rule gslt-il-produced-member-here
      (head
        (gslt-il-produced-member
          (gslt-il-produced-cons ?item ?rest) ?item))
      (body))
    (rule gslt-il-produced-member-rest
      (head
        (gslt-il-produced-member
          (gslt-il-produced-cons ?head ?rest) ?item))
      (body (gslt-il-produced-member ?rest ?item)))

    (rule gslt-il-observe-produced-command
      (head
        (gslt-il-observe
          (gslt-il-step-request ?request-occurrence ?command)
          ?bag
          (gslt-il-observed-evidence ?producer-evidence)
          ?result))
      (body
        (gslt-il-produced-member
          ?bag (gslt-produced ?producer-evidence ?result))))
  ))
