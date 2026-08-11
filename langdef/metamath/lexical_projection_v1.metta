(gslt-presentation-v1 MetamathLexicalProjectionV1
  (signature)
  (equations)
  (rewrites
    (rule mm-lexical-root-comment
      (head (source-lexical-root comment))
      (body))
    (rule mm-lexical-root-label-token
      (head (source-lexical-root label-token-lexeme))
      (body))
    (rule mm-lexical-root-proof-label-token
      (head (source-lexical-root proof-label-token-lexeme))
      (body))
    (rule mm-lexical-root-symbol-token
      (head (source-lexical-root symbol-token-lexeme))
      (body))
    (rule mm-lexical-root-include-token
      (head (source-lexical-root include-token-lexeme))
      (body))
    (rule mm-lexical-root-compressed-token
      (head (source-lexical-root compressed-token-lexeme))
      (body))
    (rule mm-lexical-root-constant-keyword
      (head (source-lexical-root kw-constant-lexeme))
      (body))
    (rule mm-lexical-root-variable-keyword
      (head (source-lexical-root kw-variable-lexeme))
      (body))
    (rule mm-lexical-root-disjoint-keyword
      (head (source-lexical-root kw-disjoint-lexeme))
      (body))
    (rule mm-lexical-root-floating-keyword
      (head (source-lexical-root kw-float-lexeme))
      (body))
    (rule mm-lexical-root-essential-keyword
      (head (source-lexical-root kw-essential-lexeme))
      (body))
    (rule mm-lexical-root-axiom-keyword
      (head (source-lexical-root kw-axiom-lexeme))
      (body))
    (rule mm-lexical-root-theorem-keyword
      (head (source-lexical-root kw-theorem-lexeme))
      (body))
    (rule mm-lexical-root-proof-keyword
      (head (source-lexical-root kw-proof-lexeme))
      (body))
    (rule mm-lexical-root-end-keyword
      (head (source-lexical-root kw-end-lexeme))
      (body))
    (rule mm-lexical-root-block-open-keyword
      (head (source-lexical-root kw-block-open-lexeme))
      (body))
    (rule mm-lexical-root-block-close-keyword
      (head (source-lexical-root kw-block-close-lexeme))
      (body))
    (rule mm-lexical-root-include-open-keyword
      (head (source-lexical-root kw-include-open-lexeme))
      (body))
    (rule mm-lexical-root-include-close-keyword
      (head (source-lexical-root kw-include-close-lexeme))
      (body))
    (rule mm-lexical-root-left-parenthesis
      (head (source-lexical-root left-paren-lexeme))
      (body))
    (rule mm-lexical-root-right-parenthesis
      (head (source-lexical-root right-paren-lexeme))
      (body))
    (rule mm-lexical-root-unknown-proof
      (head (source-lexical-root unknown-proof-lexeme))
      (body))
  ))
