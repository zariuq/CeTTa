; Generic relation-store programs for staged source-state elaboration.
; Guest presentations declare tables, literal values, and ordered actions.
; The runtime sees only validated numeric table, role, operation, and opcode
; identities produced by the compiler.

(gslt-presentation-v1 RelationalStateProgramCoreV1
  (signature
    (operator fold-contract-role 2)
    (operator source-state-action 3)
    (operator source-state-active-source-policy 1)
    (operator source-state-completed-source-policy 1)
    (operator source-state-depth 1)
    (operator source-state-final-action 2)
    (operator source-state-index 1)
    (operator source-state-lifetime 1)
    (operator source-state-literal 1)
    (operator source-state-operation 1)
    (operator source-state-operation-role 2)
    (operator source-state-proof-decoder-number 1)
    (operator source-state-proof-machine 2)
    (operator source-state-proof-unknown-policy 1)
    (operator source-state-table 4)
    (operator source-state-table-shape 2)
    (operator source-state-write-policy 1)
    (operator state-action-valid 2)
    (operator state-byte-literal-v1 1)
    (operator state-check-proof-compressed-v1 5)
    (operator state-check-proof-normal-v1 4)
    (operator state-copy-each-row-v1 4)
    (operator state-copy-each-row-matching-v1 6)
    (operator state-copy-operand-valid 3)
    (operator state-copy-row-valid 4)
    (operator state-final-action-valid 1)
    (operator state-insert-each-v1 4)
    (operator state-insert-each-matching-v1 6)
    (operator state-insert-row-v1 3)
    (operator state-insert-unordered-pairs-v1 3)
    (operator state-literal-v1 1)
    (operator state-operand-valid 2)
    (operator state-require-each-v1 3)
    (operator state-require-each-key-absent-v1 3)
    (operator state-require-row-v1 2)
    (operator state-require-row-key-absent-v1 2)
    (operator state-resolve-source-v1 3)
    (operator state-role-at-v1 2)
    (operator state-role-list-v1 2)
    (operator state-operation-operand-valid 2)
    (operator state-operation-row-valid 3)
    (operator state-proof-decoder-v1 11)
    (operator state-proof-kinds-v1 6)
    (operator state-proof-machine-valid 1)
    (operator state-proof-tables-v1 9)
    (operator state-require-depth-v1 1)
    (operator state-row-cons-v1 2)
    (operator state-row-length-step 2)
    (operator state-row-valid 3)
    (operator state-source-column-v1 1)
    (operator state-source-column-valid 2)
    (operator state-stack-proof-machine-v1 3)
  )
  (equations)
  (rewrites
    (rule state-table-shape-1-1
      (head (source-state-table-shape 1 1))
      (body))
    (rule state-table-shape-2-1
      (head (source-state-table-shape 2 1))
      (body))
    (rule state-table-shape-2-2
      (head (source-state-table-shape 2 2))
      (body))
    (rule state-table-shape-3-1
      (head (source-state-table-shape 3 1))
      (body))
    (rule state-table-shape-3-2
      (head (source-state-table-shape 3 2))
      (body))
    (rule state-table-shape-3-3
      (head (source-state-table-shape 3 3))
      (body))
    (rule state-table-shape-4-1
      (head (source-state-table-shape 4 1))
      (body))
    (rule state-table-shape-4-2
      (head (source-state-table-shape 4 2))
      (body))
    (rule state-table-shape-4-3
      (head (source-state-table-shape 4 3))
      (body))
    (rule state-table-shape-4-4
      (head (source-state-table-shape 4 4))
      (body))

    (rule state-row-length-step-0-1
      (head (state-row-length-step 0 1))
      (body))
    (rule state-row-length-step-1-2
      (head (state-row-length-step 1 2))
      (body))
    (rule state-row-length-step-2-3
      (head (state-row-length-step 2 3))
      (body))
    (rule state-row-length-step-3-4
      (head (state-row-length-step 3 4))
      (body))

    (rule state-fold-contract-role-one
      (head (fold-contract-role (fold-one ?role) ?role))
      (body (source-fold-role ?role)))
    (rule state-fold-contract-role-plus
      (head (fold-contract-role (fold-plus ?role) ?role))
      (body (source-fold-role ?role)))
    (rule state-fold-contract-role-star
      (head (fold-contract-role (fold-star ?role) ?role))
      (body (source-fold-role ?role)))
    (rule state-fold-contract-role-seq-left
      (head (fold-contract-role (fold-seq ?left ?right) ?role))
      (body (fold-contract-role ?left ?role)))
    (rule state-fold-contract-role-seq-right
      (head (fold-contract-role (fold-seq ?left ?right) ?role))
      (body (fold-contract-role ?right ?role)))

    (rule state-operation-from-node
      (head (source-state-operation ?operation))
      (body (source-fold-node ?node ?operation ?contract)))
    (rule state-operation-from-shift
      (head (source-state-operation ?operation))
      (body (source-fold-shift ?role ?operation)))
    (rule state-operation-role-from-node
      (head (source-state-operation-role ?operation ?role))
      (body
        (source-fold-node ?node ?operation ?contract)
        (fold-contract-role ?contract ?role)))
    (rule state-operation-role-from-shift
      (head (source-state-operation-role ?operation ?role))
      (body (source-fold-shift ?role ?operation)))

    (rule state-completed-source-policy-skip
      (head
        (source-state-completed-source-policy
          state-skip-completed-source-v1))
      (body))
    (rule state-completed-source-policy-reject
      (head
        (source-state-completed-source-policy
          state-reject-completed-source-v1))
      (body))
    (rule state-active-source-policy-skip
      (head
        (source-state-active-source-policy
          state-skip-active-source-v1))
      (body))
    (rule state-active-source-policy-reject
      (head
        (source-state-active-source-policy
          state-reject-active-source-v1))
      (body))

    (rule state-operand-valid-input
      (head (state-operand-valid ?role state-input-v1))
      (body (source-fold-role ?role)))
    (rule state-operand-valid-literal
      (head
        (state-operand-valid ?role (state-literal-v1 ?literal)))
      (body
        (source-fold-role ?role)
        (source-state-literal ?literal)))
    (rule state-row-valid-empty
      (head (state-row-valid ?role state-row-nil-v1 0))
      (body (source-fold-role ?role)))
    (rule state-row-valid-cons
      (head
        (state-row-valid
          ?role
          (state-row-cons-v1 ?operand ?rest)
          ?next-length))
      (body
        (state-operand-valid ?role ?operand)
        (state-row-valid ?role ?rest ?length)
        (state-row-length-step ?length ?next-length)))

    (rule state-operation-operand-valid-role-at
      (head
        (state-operation-operand-valid
          ?operation (state-role-at-v1 ?role ?index)))
      (body
        (source-state-operation-role ?operation ?role)
        (source-state-index ?index)))
    (rule state-operation-operand-valid-role-list
      (head
        (state-operation-operand-valid
          ?operation (state-role-list-v1 ?role ?index)))
      (body
        (source-state-operation-role ?operation ?role)
        (source-state-index ?index)))
    (rule state-operation-operand-valid-input
      (head
        (state-operation-operand-valid ?operation state-input-v1))
      (body (source-state-operation ?operation)))
    (rule state-operation-operand-valid-literal
      (head
        (state-operation-operand-valid
          ?operation (state-literal-v1 ?literal)))
      (body
        (source-state-operation ?operation)
        (source-state-literal ?literal)))
    (rule state-operation-row-valid-empty
      (head
        (state-operation-row-valid
          ?operation state-row-nil-v1 0))
      (body (source-state-operation ?operation)))
    (rule state-operation-row-valid-cons
      (head
        (state-operation-row-valid
          ?operation
          (state-row-cons-v1 ?operand ?rest)
          ?next-length))
      (body
        (state-operation-operand-valid ?operation ?operand)
        (state-operation-row-valid ?operation ?rest ?length)
        (state-row-length-step ?length ?next-length)))

    (rule state-source-column-valid-1-0
      (head (state-source-column-valid 1 0))
      (body))
    (rule state-source-column-valid-2-0
      (head (state-source-column-valid 2 0))
      (body))
    (rule state-source-column-valid-2-1
      (head (state-source-column-valid 2 1))
      (body))
    (rule state-source-column-valid-3-0
      (head (state-source-column-valid 3 0))
      (body))
    (rule state-source-column-valid-3-1
      (head (state-source-column-valid 3 1))
      (body))
    (rule state-source-column-valid-3-2
      (head (state-source-column-valid 3 2))
      (body))
    (rule state-source-column-valid-4-0
      (head (state-source-column-valid 4 0))
      (body))
    (rule state-source-column-valid-4-1
      (head (state-source-column-valid 4 1))
      (body))
    (rule state-source-column-valid-4-2
      (head (state-source-column-valid 4 2))
      (body))
    (rule state-source-column-valid-4-3
      (head (state-source-column-valid 4 3))
      (body))
    (rule state-copy-operand-valid-operation
      (head
        (state-copy-operand-valid
          ?operation ?source-arity ?operand))
      (body
        (source-state-table-shape ?source-arity ?source-key-arity)
        (state-operation-operand-valid ?operation ?operand)))
    (rule state-copy-operand-valid-source-column
      (head
        (state-copy-operand-valid
          ?operation
          ?source-arity
          (state-source-column-v1 ?column)))
      (body
        (source-state-operation ?operation)
        (state-source-column-valid ?source-arity ?column)))
    (rule state-copy-operand-valid-source-row-index
      (head
        (state-copy-operand-valid
          ?operation ?source-arity state-source-row-index-v1))
      (body
        (source-state-operation ?operation)
        (source-state-table-shape ?source-arity ?source-key-arity)))
    (rule state-copy-operand-valid-source-match-index
      (head
        (state-copy-operand-valid
          ?operation ?source-arity state-source-match-index-v1))
      (body
        (source-state-operation ?operation)
        (source-state-table-shape ?source-arity ?source-key-arity)))
    (rule state-copy-row-valid-empty
      (head
        (state-copy-row-valid
          ?operation ?source-arity state-row-nil-v1 0))
      (body
        (source-state-operation ?operation)
        (source-state-table-shape ?source-arity ?source-key-arity)))
    (rule state-copy-row-valid-cons
      (head
        (state-copy-row-valid
          ?operation
          ?source-arity
          (state-row-cons-v1 ?operand ?rest)
          ?next-length))
      (body
        (state-copy-operand-valid
          ?operation ?source-arity ?operand)
        (state-copy-row-valid
          ?operation ?source-arity ?rest ?length)
        (state-row-length-step ?length ?next-length)))

    (rule state-action-valid-require-depth
      (head
        (state-action-valid
          ?operation (state-require-depth-v1 ?depth)))
      (body
        (source-state-operation ?operation)
        (source-state-depth ?depth)))
    (rule state-action-valid-push-scope
      (head (state-action-valid ?operation state-push-scope-v1))
      (body (source-state-operation ?operation)))
    (rule state-action-valid-pop-scope
      (head (state-action-valid ?operation state-pop-scope-v1))
      (body (source-state-operation ?operation)))
    (rule state-action-valid-noop
      (head (state-action-valid ?operation state-noop-v1))
      (body (source-state-operation ?operation)))
    (rule state-action-valid-resolve-source
      (head
        (state-action-valid
          ?operation
          (state-resolve-source-v1
            ?role ?completed-policy ?active-policy)))
      (body
        (source-state-operation-role ?operation ?role)
        (source-state-completed-source-policy ?completed-policy)
        (source-state-active-source-policy ?active-policy)))
    (rule state-action-valid-insert-each
      (head
        (state-action-valid
          ?operation
          (state-insert-each-v1
            ?role ?table ?write-policy ?row)))
      (body
        (source-state-operation-role ?operation ?role)
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-table-shape ?arity ?key-arity)
        (source-state-lifetime ?lifetime)
        (source-state-write-policy ?write-policy)
        (state-row-valid ?role ?row ?arity)))
    (rule state-action-valid-require-each
      (head
        (state-action-valid
          ?operation
          (state-require-each-v1 ?role ?table ?row)))
      (body
        (source-state-operation-role ?operation ?role)
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-table-shape ?arity ?key-arity)
        (source-state-lifetime ?lifetime)
        (state-row-valid ?role ?row ?arity)))
    (rule state-action-valid-require-each-key-absent
      (head
        (state-action-valid
          ?operation
          (state-require-each-key-absent-v1 ?role ?table ?row)))
      (body
        (source-state-operation-role ?operation ?role)
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-table-shape ?arity ?key-arity)
        (source-state-lifetime ?lifetime)
        (state-row-valid ?role ?row ?key-arity)))
    (rule state-action-valid-insert-unordered-pairs
      (head
        (state-action-valid
          ?operation
          (state-insert-unordered-pairs-v1
            ?role ?table ?write-policy)))
      (body
        (source-state-operation-role ?operation ?role)
        (source-state-table ?table 2 2 ?lifetime)
        (source-state-lifetime ?lifetime)
        (source-state-write-policy ?write-policy)))
    (rule state-action-valid-require-row
      (head
        (state-action-valid
          ?operation (state-require-row-v1 ?table ?row)))
      (body
        (source-state-operation ?operation)
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-table-shape ?arity ?key-arity)
        (source-state-lifetime ?lifetime)
        (state-operation-row-valid ?operation ?row ?arity)))
    (rule state-action-valid-require-row-key-absent
      (head
        (state-action-valid
          ?operation
          (state-require-row-key-absent-v1 ?table ?row)))
      (body
        (source-state-operation ?operation)
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-table-shape ?arity ?key-arity)
        (source-state-lifetime ?lifetime)
        (state-operation-row-valid ?operation ?row ?key-arity)))
    (rule state-action-valid-insert-row
      (head
        (state-action-valid
          ?operation
          (state-insert-row-v1 ?table ?write-policy ?row)))
      (body
        (source-state-operation ?operation)
        (source-state-table ?table ?arity ?key-arity ?lifetime)
        (source-state-table-shape ?arity ?key-arity)
        (source-state-lifetime ?lifetime)
        (source-state-write-policy ?write-policy)
        (state-operation-row-valid ?operation ?row ?arity)))
    (rule state-action-valid-insert-each-matching
      (head
        (state-action-valid
          ?operation
          (state-insert-each-matching-v1
            ?role
            ?condition-table
            ?condition-row
            ?target-table
            ?write-policy
            ?target-row)))
      (body
        (source-state-operation-role ?operation ?role)
        (source-state-table
          ?condition-table
          ?condition-arity
          ?condition-key-arity
          ?condition-lifetime)
        (source-state-table-shape
          ?condition-arity ?condition-key-arity)
        (source-state-lifetime ?condition-lifetime)
        (state-operation-row-valid
          ?operation ?condition-row ?condition-arity)
        (source-state-table
          ?target-table ?target-arity ?target-key-arity ?target-lifetime)
        (source-state-table-shape ?target-arity ?target-key-arity)
        (source-state-lifetime ?target-lifetime)
        (source-state-write-policy ?write-policy)
        (state-operation-row-valid
          ?operation ?target-row ?target-arity)))
    (rule state-action-valid-copy-each-row
      (head
        (state-action-valid
          ?operation
          (state-copy-each-row-v1
            ?source-table ?target-table ?write-policy ?target-row)))
      (body
        (source-state-operation ?operation)
        (source-state-table
          ?source-table ?source-arity ?source-key-arity ?source-lifetime)
        (source-state-table-shape ?source-arity ?source-key-arity)
        (source-state-lifetime ?source-lifetime)
        (source-state-table
          ?target-table ?target-arity ?target-key-arity ?target-lifetime)
        (source-state-table-shape ?target-arity ?target-key-arity)
        (source-state-lifetime ?target-lifetime)
        (source-state-write-policy ?write-policy)
        (state-copy-row-valid
          ?operation ?source-arity ?target-row ?target-arity)))
    (rule state-action-valid-copy-each-row-matching
      (head
        (state-action-valid
          ?operation
          (state-copy-each-row-matching-v1
            ?source-table
            ?condition-table
            ?condition-row
            ?target-table
            ?write-policy
            ?target-row)))
      (body
        (source-state-operation ?operation)
        (source-state-table
          ?source-table ?source-arity ?source-key-arity ?source-lifetime)
        (source-state-table-shape ?source-arity ?source-key-arity)
        (source-state-lifetime ?source-lifetime)
        (source-state-table
          ?condition-table
          ?condition-arity
          ?condition-key-arity
          ?condition-lifetime)
        (source-state-table-shape
          ?condition-arity ?condition-key-arity)
        (source-state-lifetime ?condition-lifetime)
        (state-copy-row-valid
          ?operation ?source-arity ?condition-row ?condition-arity)
        (source-state-table
          ?target-table ?target-arity ?target-key-arity ?target-lifetime)
        (source-state-table-shape ?target-arity ?target-key-arity)
        (source-state-lifetime ?target-lifetime)
        (source-state-write-policy ?write-policy)
        (state-copy-row-valid
          ?operation ?source-arity ?target-row ?target-arity)))

    (rule state-proof-machine-valid-stack
      (head
        (state-proof-machine-valid
          (state-stack-proof-machine-v1
            (state-proof-tables-v1
              ?label-kind-table
              ?formula-table
              ?binder-variable-table
              ?mandatory-variable-table
              ?assertion-hypothesis-table
              ?assertion-disjoint-table
              ?active-hypothesis-table
              ?active-disjoint-table
              ?symbol-kind-table)
            (state-proof-kinds-v1
              (state-literal-v1 ?binder-kind)
              (state-literal-v1 ?matching-kind)
              (state-literal-v1 ?rule-kind-first)
              (state-literal-v1 ?rule-kind-second)
              (state-literal-v1 ?variable-kind)
              (state-byte-literal-v1 ?unknown-token-byte))
            (state-proof-decoder-v1
              ?terminal-low ?terminal-high
              ?continuation-low ?continuation-high
              ?save-byte ?unknown-byte
              ?terminal-radix ?terminal-bias
              ?continuation-radix ?continuation-bias
              ?unknown-policy))))
      (body
        (source-state-table ?label-kind-table 2 1 ?label-kind-life)
        (source-state-lifetime ?label-kind-life)
        (source-state-table ?formula-table 2 1 ?formula-life)
        (source-state-lifetime ?formula-life)
        (source-state-table ?binder-variable-table 2 1 ?binder-life)
        (source-state-lifetime ?binder-life)
        (source-state-table ?mandatory-variable-table 2 2 ?mandatory-life)
        (source-state-lifetime ?mandatory-life)
        (source-state-table ?assertion-hypothesis-table 3 2 ?hyp-life)
        (source-state-lifetime ?hyp-life)
        (source-state-table ?assertion-disjoint-table 3 3 ?assertion-dv-life)
        (source-state-lifetime ?assertion-dv-life)
        (source-state-table ?active-hypothesis-table 4 1 ?active-hyp-life)
        (source-state-lifetime ?active-hyp-life)
        (source-state-table ?active-disjoint-table 2 2 ?active-dv-life)
        (source-state-lifetime ?active-dv-life)
        (source-state-table ?symbol-kind-table 2 1 ?symbol-life)
        (source-state-lifetime ?symbol-life)
        (source-state-literal ?binder-kind)
        (source-state-literal ?matching-kind)
        (source-state-literal ?rule-kind-first)
        (source-state-literal ?rule-kind-second)
        (source-state-literal ?variable-kind)
        (source-state-proof-decoder-number ?unknown-token-byte)
        (source-state-proof-decoder-number ?terminal-low)
        (source-state-proof-decoder-number ?terminal-high)
        (source-state-proof-decoder-number ?continuation-low)
        (source-state-proof-decoder-number ?continuation-high)
        (source-state-proof-decoder-number ?save-byte)
        (source-state-proof-decoder-number ?unknown-byte)
        (source-state-proof-decoder-number ?terminal-radix)
        (source-state-proof-decoder-number ?terminal-bias)
        (source-state-proof-decoder-number ?continuation-radix)
        (source-state-proof-decoder-number ?continuation-bias)
        (source-state-proof-unknown-policy ?unknown-policy)))

    (rule state-action-valid-check-proof-normal
      (head
        (state-action-valid
          ?operation
          (state-check-proof-normal-v1
            ?machine ?label-role ?formula-role ?proof-role)))
      (body
        (source-state-proof-machine ?machine ?configuration)
        (state-proof-machine-valid ?configuration)
        (source-state-operation-role ?operation ?label-role)
        (source-state-operation-role ?operation ?formula-role)
        (source-state-operation-role ?operation ?proof-role)))
    (rule state-action-valid-check-proof-compressed
      (head
        (state-action-valid
          ?operation
          (state-check-proof-compressed-v1
            ?machine ?label-role ?formula-role ?header-role ?code-role)))
      (body
        (source-state-proof-machine ?machine ?configuration)
        (state-proof-machine-valid ?configuration)
        (source-state-operation-role ?operation ?label-role)
        (source-state-operation-role ?operation ?formula-role)
        (source-state-operation-role ?operation ?header-role)
        (source-state-operation-role ?operation ?code-role)))

    (rule state-final-action-valid-require-depth
      (head
        (state-final-action-valid
          (state-require-depth-v1 ?depth)))
      (body (source-state-depth ?depth)))
    (rule state-final-action-valid-noop
      (head (state-final-action-valid state-noop-v1))
      (body))
  ))
