; First source-state tranche: global symbol classification, scoped variable
; activity, and balanced scope transitions. Operations without an authored
; program remain unsupported and therefore fail closed during staged import.

(gslt-presentation-v1 MetamathSourceStateV1
  (signature)
  (equations)
  (rewrites
    (rule mm-state-lifetime-persistent
      (head (source-state-lifetime state-persistent-v1))
      (body))
    (rule mm-state-lifetime-scoped
      (head (source-state-lifetime state-scoped-v1))
      (body))
    (rule mm-state-lifetime-transactional
      (head (source-state-lifetime state-transactional-v1))
      (body))
    (rule mm-state-policy-require-absent
      (head
        (source-state-write-policy state-require-key-absent-v1))
      (body))
    (rule mm-state-policy-insert-or-equal
      (head
        (source-state-write-policy state-insert-or-require-equal-v1))
      (body))
    (rule mm-state-depth-zero
      (head (source-state-depth 0))
      (body))
    (rule mm-state-index-zero
      (head (source-state-index 0))
      (body))
    (rule mm-state-index-one
      (head (source-state-index 1))
      (body))
    (rule mm-state-literal-constant
      (head (source-state-literal mm-symbol-constant))
      (body))
    (rule mm-state-literal-variable
      (head (source-state-literal mm-symbol-variable))
      (body))
    (rule mm-state-literal-floating
      (head (source-state-literal mm-label-floating))
      (body))
    (rule mm-state-literal-essential
      (head (source-state-literal mm-label-essential))
      (body))
    (rule mm-state-literal-axiom
      (head (source-state-literal mm-label-axiom))
      (body))
    (rule mm-state-literal-no-variable
      (head (source-state-literal mm-no-variable))
      (body))

    (rule mm-state-table-symbol-kind
      (head
        (source-state-table
          mm-state-symbol-kind 2 1 state-persistent-v1))
      (body))
    (rule mm-state-table-active-variable
      (head
        (source-state-table
          mm-state-active-variable 1 1 state-scoped-v1))
      (body))
    (rule mm-state-table-disjoint-variable
      (head
        (source-state-table
          mm-state-disjoint-variable 2 2 state-scoped-v1))
      (body))
    (rule mm-state-table-expression-symbol
      (head
        (source-state-table
          mm-state-expression-symbol 1 1 state-scoped-v1))
      (body))
    (rule mm-state-table-label-kind
      (head
        (source-state-table
          mm-state-label-kind 2 1 state-persistent-v1))
      (body))
    (rule mm-state-table-label-name
      (head
        (source-state-table
          mm-state-label-name 1 1 state-persistent-v1))
      (body))
    (rule mm-state-table-active-hypothesis
      (head
        (source-state-table
          mm-state-active-hypothesis 4 1 state-scoped-v1))
      (body))
    (rule mm-state-table-active-floating-variable
      (head
        (source-state-table
          mm-state-active-floating-variable 1 1 state-scoped-v1))
      (body))
    (rule mm-state-table-floating-variable
      (head
        (source-state-table
          mm-state-floating-variable 2 1 state-persistent-v1))
      (body))
    (rule mm-state-table-formula
      (head
        (source-state-table
          mm-state-formula 2 1 state-persistent-v1))
      (body))
    (rule mm-state-table-active-essential-variable
      (head
        (source-state-table
          mm-state-active-essential-variable 1 1 state-scoped-v1))
      (body))
    (rule mm-state-table-assertion-mandatory-variable
      (head
        (source-state-table
          mm-state-assertion-mandatory-variable
          2 2 state-persistent-v1))
      (body))
    (rule mm-state-table-assertion-active-hypothesis
      (head
        (source-state-table
          mm-state-assertion-active-hypothesis
          2 2 state-persistent-v1))
      (body))
    (rule mm-state-table-assertion-ordered-hypothesis
      (head
        (source-state-table
          mm-state-assertion-ordered-hypothesis
          3 2 state-persistent-v1))
      (body))
    (rule mm-state-table-assertion-frame-hypothesis
      (head
        (source-state-table
          mm-state-assertion-frame-hypothesis
          2 2 state-persistent-v1))
      (body))
    (rule mm-state-table-assertion-ordered-frame-hypothesis
      (head
        (source-state-table
          mm-state-assertion-ordered-frame-hypothesis
          3 2 state-persistent-v1))
      (body))
    (rule mm-state-table-assertion-disjoint-left-candidate
      (head
        (source-state-table
          mm-state-assertion-disjoint-left-candidate
          3 3 state-transactional-v1))
      (body))
    (rule mm-state-table-assertion-disjoint-variable
      (head
        (source-state-table
          mm-state-assertion-disjoint-variable
          3 3 state-persistent-v1))
      (body))
    (rule mm-state-table-assertion-ordered-disjoint-variable
      (head
        (source-state-table
          mm-state-assertion-ordered-disjoint-variable
          4 2 state-persistent-v1))
      (body))

    (rule mm-state-open-scope
      (head
        (source-state-action
          mm-open-scope 0 state-push-scope-v1))
      (body))
    (rule mm-state-close-scope
      (head
        (source-state-action
          mm-close-scope 0 state-pop-scope-v1))
      (body))
    (rule mm-state-complete-block
      (head
        (source-state-action
          mm-complete-block 0 state-noop-v1))
      (body))

    (rule mm-state-include-resolve
      (head
        (source-state-action
          mm-resolve-include
          0
          (state-resolve-source-v1
            mm-include-path
            state-skip-completed-source-v1
            state-skip-active-source-v1)))
      (body))

    (rule mm-state-constants-top-level
      (head
        (source-state-action
          mm-declare-constants 0 (state-require-depth-v1 0)))
      (body))
    (rule mm-state-constants-not-labels
      (head
        (source-state-action
          mm-declare-constants
          1
          (state-require-each-key-absent-v1
            mm-symbol
            mm-state-label-name
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-state-constants-classify
      (head
        (source-state-action
          mm-declare-constants
          2
          (state-insert-each-v1
            mm-symbol
            mm-state-symbol-kind
            state-require-key-absent-v1
            (state-row-cons-v1
              state-input-v1
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-constant)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-constants-enable-expression
      (head
        (source-state-action
          mm-declare-constants
          3
          (state-insert-each-v1
            mm-symbol
            mm-state-expression-symbol
            state-insert-or-require-equal-v1
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))

    (rule mm-state-variables-not-labels
      (head
        (source-state-action
          mm-declare-variables
          0
          (state-require-each-key-absent-v1
            mm-symbol
            mm-state-label-name
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-state-variables-classify
      (head
        (source-state-action
          mm-declare-variables
          1
          (state-insert-each-v1
            mm-symbol
            mm-state-symbol-kind
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              state-input-v1
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-variable)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-variables-activate
      (head
        (source-state-action
          mm-declare-variables
          2
          (state-insert-each-v1
            mm-symbol
            mm-state-active-variable
            state-require-key-absent-v1
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-state-floating-label-not-symbol
      (head
        (source-state-action
          mm-declare-floating
          0
          (state-require-row-key-absent-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-floating-label-fresh
      (head
        (source-state-action
          mm-declare-floating
          1
          (state-require-row-key-absent-v1
            mm-state-label-name
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-floating-typecode-constant
      (head
        (source-state-action
          mm-declare-floating
          2
          (state-require-row-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-symbol 0)
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-constant)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-floating-variable-active
      (head
        (source-state-action
          mm-declare-floating
          3
          (state-require-row-v1
            mm-state-active-variable
            (state-row-cons-v1
              (state-role-at-v1 mm-symbol 1)
              state-row-nil-v1))))
      (body))
    (rule mm-state-floating-active-unique
      (head
        (source-state-action
          mm-declare-floating
          4
          (state-insert-row-v1
            mm-state-active-floating-variable
            state-require-key-absent-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-symbol 1)
              state-row-nil-v1))))
      (body))
    (rule mm-state-floating-record-active
      (head
        (source-state-action
          mm-declare-floating
          5
          (state-insert-row-v1
            mm-state-active-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-floating)
                (state-row-cons-v1
                  (state-role-at-v1 mm-symbol 1)
                  (state-row-cons-v1
                    (state-role-list-v1 mm-symbol 0)
                    state-row-nil-v1)))))))
      (body))
    (rule mm-state-floating-enable-expression
      (head
        (source-state-action
          mm-declare-floating
          6
          (state-insert-row-v1
            mm-state-expression-symbol
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-symbol 1)
              state-row-nil-v1))))
      (body))
    (rule mm-state-floating-register-label-name
      (head
        (source-state-action
          mm-declare-floating
          7
          (state-insert-row-v1
            mm-state-label-name
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-floating-register-label-kind
      (head
        (source-state-action
          mm-declare-floating
          8
          (state-insert-row-v1
            mm-state-label-kind
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-floating)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-floating-record-variable
      (head
        (source-state-action
          mm-declare-floating
          9
          (state-insert-row-v1
            mm-state-floating-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-role-at-v1 mm-symbol 1)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-floating-record-formula
      (head
        (source-state-action
          mm-declare-floating
          10
          (state-insert-row-v1
            mm-state-formula
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-role-list-v1 mm-symbol 0)
                state-row-nil-v1)))))
      (body))

    (rule mm-state-essential-label-not-symbol
      (head
        (source-state-action
          mm-declare-essential
          0
          (state-require-row-key-absent-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-essential-label-fresh
      (head
        (source-state-action
          mm-declare-essential
          1
          (state-require-row-key-absent-v1
            mm-state-label-name
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-essential-typecode-constant
      (head
        (source-state-action
          mm-declare-essential
          2
          (state-require-row-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-symbol 0)
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-constant)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-essential-symbols-usable
      (head
        (source-state-action
          mm-declare-essential
          3
          (state-require-each-v1
            mm-symbol
            mm-state-expression-symbol
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-state-essential-record-active
      (head
        (source-state-action
          mm-declare-essential
          4
          (state-insert-row-v1
            mm-state-active-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-essential)
                (state-row-cons-v1
                  (state-literal-v1 mm-no-variable)
                  (state-row-cons-v1
                    (state-role-list-v1 mm-symbol 0)
                    state-row-nil-v1)))))))
      (body))
    (rule mm-state-essential-record-formula
      (head
        (source-state-action
          mm-declare-essential
          5
          (state-insert-row-v1
            mm-state-formula
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-role-list-v1 mm-symbol 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-essential-collect-variables
      (head
        (source-state-action
          mm-declare-essential
          6
          (state-insert-each-matching-v1
            mm-symbol
            mm-state-symbol-kind
            (state-row-cons-v1
              state-input-v1
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-variable)
                state-row-nil-v1))
            mm-state-active-essential-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-state-essential-register-label-name
      (head
        (source-state-action
          mm-declare-essential
          7
          (state-insert-row-v1
            mm-state-label-name
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-essential-register-label-kind
      (head
        (source-state-action
          mm-declare-essential
          8
          (state-insert-row-v1
            mm-state-label-kind
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-essential)
                state-row-nil-v1)))))
      (body))

    (rule mm-state-axiom-label-not-symbol
      (head
        (source-state-action
          mm-declare-axiom
          0
          (state-require-row-key-absent-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-axiom-label-fresh
      (head
        (source-state-action
          mm-declare-axiom
          1
          (state-require-row-key-absent-v1
            mm-state-label-name
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-axiom-typecode-constant
      (head
        (source-state-action
          mm-declare-axiom
          2
          (state-require-row-v1
            mm-state-symbol-kind
            (state-row-cons-v1
              (state-role-at-v1 mm-symbol 0)
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-constant)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-axiom-symbols-usable
      (head
        (source-state-action
          mm-declare-axiom
          3
          (state-require-each-v1
            mm-symbol
            mm-state-expression-symbol
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-state-axiom-collect-formula-variables
      (head
        (source-state-action
          mm-declare-axiom
          4
          (state-insert-each-matching-v1
            mm-symbol
            mm-state-symbol-kind
            (state-row-cons-v1
              state-input-v1
              (state-row-cons-v1
                (state-literal-v1 mm-symbol-variable)
                state-row-nil-v1))
            mm-state-assertion-mandatory-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                state-input-v1
                state-row-nil-v1)))))
      (body))
    (rule mm-state-axiom-collect-essential-variables
      (head
        (source-state-action
          mm-declare-axiom
          5
          (state-copy-each-row-v1
            mm-state-active-essential-variable
            mm-state-assertion-mandatory-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-axiom-order-active-hypotheses
      (head
        (source-state-action
          mm-declare-axiom
          6
          (state-copy-each-row-v1
            mm-state-active-hypothesis
            mm-state-assertion-ordered-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                state-source-row-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 0)
                  state-row-nil-v1))))))
      (body))
    (rule mm-state-axiom-select-essential-frame-hypotheses
      (head
        (source-state-action
          mm-declare-axiom
          7
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-label-kind
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-essential)
                state-row-nil-v1))
            mm-state-assertion-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-axiom-select-floating-frame-hypotheses
      (head
        (source-state-action
          mm-declare-axiom
          8
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-axiom-order-frame-hypotheses
      (head
        (source-state-action
          mm-declare-axiom
          9
          (state-copy-each-row-matching-v1
            mm-state-active-hypothesis
            mm-state-assertion-frame-hypothesis
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1))
            mm-state-assertion-ordered-frame-hypothesis
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                state-source-match-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 0)
                  state-row-nil-v1))))))
      (body))
    (rule mm-state-axiom-select-disjoint-left-mandatory
      (head
        (source-state-action
          mm-declare-axiom
          10
          (state-copy-each-row-matching-v1
            mm-state-disjoint-variable
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                state-row-nil-v1))
            mm-state-assertion-disjoint-left-candidate
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-source-column-v1 0)
                (state-row-cons-v1
                  (state-source-column-v1 1)
                  state-row-nil-v1))))))
      (body))
    (rule mm-state-axiom-select-disjoint-right-mandatory
      (head
        (source-state-action
          mm-declare-axiom
          11
          (state-copy-each-row-matching-v1
            mm-state-assertion-disjoint-left-candidate
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-disjoint-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 1)
                (state-row-cons-v1
                  (state-source-column-v1 2)
                  state-row-nil-v1))))))
      (body))
    (rule mm-state-axiom-order-disjoint-mandatory
      (head
        (source-state-action
          mm-declare-axiom
          12
          (state-copy-each-row-matching-v1
            mm-state-assertion-disjoint-left-candidate
            mm-state-assertion-mandatory-variable
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                (state-source-column-v1 2)
                state-row-nil-v1))
            mm-state-assertion-ordered-disjoint-variable
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-source-column-v1 0)
              (state-row-cons-v1
                state-source-match-index-v1
                (state-row-cons-v1
                  (state-source-column-v1 1)
                  (state-row-cons-v1
                    (state-source-column-v1 2)
                    state-row-nil-v1)))))))
      (body))
    (rule mm-state-axiom-record-formula
      (head
        (source-state-action
          mm-declare-axiom
          13
          (state-insert-row-v1
            mm-state-formula
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-role-list-v1 mm-symbol 0)
                state-row-nil-v1)))))
      (body))
    (rule mm-state-axiom-register-label-name
      (head
        (source-state-action
          mm-declare-axiom
          14
          (state-insert-row-v1
            mm-state-label-name
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              state-row-nil-v1))))
      (body))
    (rule mm-state-axiom-register-label-kind
      (head
        (source-state-action
          mm-declare-axiom
          15
          (state-insert-row-v1
            mm-state-label-kind
            state-insert-or-require-equal-v1
            (state-row-cons-v1
              (state-role-at-v1 mm-label 0)
              (state-row-cons-v1
                (state-literal-v1 mm-label-axiom)
                state-row-nil-v1)))))
      (body))

    (rule mm-state-disjoint-variables-active
      (head
        (source-state-action
          mm-declare-disjoint
          0
          (state-require-each-v1
            mm-symbol
            mm-state-active-variable
            (state-row-cons-v1 state-input-v1 state-row-nil-v1))))
      (body))
    (rule mm-state-disjoint-variables-pairwise
      (head
        (source-state-action
          mm-declare-disjoint
          1
          (state-insert-unordered-pairs-v1
            mm-symbol
            mm-state-disjoint-variable
            state-insert-or-require-equal-v1)))
      (body))
  ))
