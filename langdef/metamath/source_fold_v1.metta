; Authored against the Metamath source grammar and operation algebra.
; Node contracts follow the source-production parameter sorts.

(gslt-presentation-v1 MetamathSyntaxFoldV1
  (signature)
  (equations)
  (rewrites
    (rule mm-fold-role-label
      (head (source-fold-role mm-label))
      (body))
    (rule mm-fold-role-symbol
      (head (source-fold-role mm-symbol))
      (body))
    (rule mm-fold-role-proof-step
      (head (source-fold-role mm-proof-step))
      (body))
    (rule mm-fold-role-compressed-word
      (head (source-fold-role mm-compressed-word))
      (body))
    (rule mm-fold-role-include-path
      (head (source-fold-role mm-include-path))
      (body))
    (rule mm-fold-role-scope-open
      (head (source-fold-role mm-scope-open))
      (body))
    (rule mm-fold-role-scope-close
      (head (source-fold-role mm-scope-close))
      (body))
    (rule mm-fold-token-label
      (head (source-fold-token label-token-lexeme mm-label))
      (body))
    (rule mm-fold-token-proof-label
      (head (source-fold-token proof-label-token-lexeme mm-proof-step))
      (body))
    (rule mm-fold-token-unknown-proof
      (head (source-fold-token unknown-proof-lexeme mm-proof-step))
      (body))
    (rule mm-fold-token-symbol
      (head (source-fold-token symbol-token-lexeme mm-symbol))
      (body))
    (rule mm-fold-token-compressed
      (head (source-fold-token compressed-token-lexeme mm-compressed-word))
      (body))
    (rule mm-fold-token-include
      (head (source-fold-token include-token-lexeme mm-include-path))
      (body))
    (rule mm-fold-token-scope-open
      (head (source-fold-token kw-block-open-lexeme mm-scope-open))
      (body))
    (rule mm-fold-token-scope-close
      (head (source-fold-token kw-block-close-lexeme mm-scope-close))
      (body))
    (rule mm-fold-shift-scope-open
      (head (source-fold-shift mm-scope-open mm-open-scope))
      (body))
    (rule mm-fold-shift-scope-close
      (head (source-fold-shift mm-scope-close mm-close-scope))
      (body))
    (rule mm-fold-node-constant
      (head (source-fold-node statement_const mm-declare-constants (fold-plus mm-symbol)))
      (body))
    (rule mm-fold-node-variable
      (head (source-fold-node statement_var mm-declare-variables (fold-plus mm-symbol)))
      (body))
    (rule mm-fold-node-disjoint
      (head (source-fold-node statement_disjoint mm-declare-disjoint (fold-seq (fold-one mm-symbol) (fold-plus mm-symbol))))
      (body))
    (rule mm-fold-node-floating
      (head (source-fold-node statement_float mm-declare-floating (fold-seq (fold-one mm-label) (fold-seq (fold-one mm-symbol) (fold-one mm-symbol)))))
      (body))
    (rule mm-fold-node-essential
      (head (source-fold-node statement_essential mm-declare-essential (fold-seq (fold-one mm-label) (fold-plus mm-symbol))))
      (body))
    (rule mm-fold-node-axiom
      (head (source-fold-node statement_axiom mm-declare-axiom (fold-seq (fold-one mm-label) (fold-plus mm-symbol))))
      (body))
    (rule mm-fold-node-theorem-normal
      (head (source-fold-node statement_theorem_normal mm-check-theorem-normal (fold-seq (fold-one mm-label) (fold-seq (fold-plus mm-symbol) (fold-plus mm-proof-step)))))
      (body))
    (rule mm-fold-node-theorem-compressed
      (head (source-fold-node statement_theorem_compressed mm-check-theorem-compressed (fold-seq (fold-one mm-label) (fold-seq (fold-plus mm-symbol) (fold-seq (fold-star mm-proof-step) (fold-plus mm-compressed-word))))))
      (body))
    (rule mm-fold-node-include
      (head (source-fold-node statement_include mm-resolve-include (fold-one mm-include-path)))
      (body))
    (rule mm-fold-node-block
      (head (source-fold-node statement_block mm-complete-block fold-none))
      (body))
  ))
