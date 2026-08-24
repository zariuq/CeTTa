; The outer parser retains math strings as scalar payloads.  This fold exposes
; those payloads to ordered MM0 declaration operations; it does not specialize
; the parser to any particular source file or delimiter environment.

(gslt-presentation-v1 MM0TextualSourceFoldV1
  (signature
    (operator cp 1)
    (operator cons 2)
    (operator node 2)
    (operator pair 2)
    (operator some 1)
    (operator different 2)
    (operator source-fold-role 1)
    (operator source-fold-token 2)
    (operator source-fold-node 3)
    (operator fold-one 1)
    (operator fold-star 1)
    (operator fold-seq 2)
    (operator MM0MathClassEntryV1 2)
    (operator MM0MathClassTableConsV1 2)
    (operator MM0MathClassLookupV1 3)
    (operator MM0DelimiterScalarV1 1)
    (operator MM0DelimiterClassMergeV1 3)
    (operator MM0DelimiterClassPutV1 4)
    (operator MM0DelimiterCharactersFoldV1 4)
    (operator MM0DelimiterCharactersAfterScalarV1 4)
    (operator MM0DelimiterEnvironmentFoldV1 3)
    (operator MM0DelimiterEnvironmentFromAstV1 2)
    (operator MM0LiveDelimiterLexV1 3))
  (equations)
  (rewrites
    (rule mm0-fold-role-math-source-v1
      (head (source-fold-role mm0-math-source-v1))
      (body))

    (rule mm0-fold-token-math-source-v1
      (head
        (source-fold-token
          mm0-math-string-lexeme mm0-math-source-v1))
      (body))

    ; One math string declares every scalar as both a left and right
    ; delimiter.  Two strings declare the first as left and the second as
    ; right.  The ordered state transition interprets that distinction from
    ; the one-or-two retained payloads.
    (rule mm0-fold-node-delimiter-v1
      (head
        (source-fold-node
          mm0-delimiter-statement
          mm0-declare-delimiters-v1
          (fold-seq
            (fold-one mm0-math-source-v1)
            (fold-star mm0-math-source-v1))))
      (body))

    (rule mm0-delimiter-scalar-v1
      (head (MM0DelimiterScalarV1 ?scalar))
      (body
        (MM0MathClassLookupV1
          ?scalar
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1
              (cp 32) MM0MathWhitespaceV1)
            (MM0MathClassTableConsV1
              (MM0MathClassEntryV1
                (cp 10) MM0MathWhitespaceV1)
              MM0MathClassTableNilV1))
          MM0MathOrdinaryV1)))

    ; A delimiter environment is runtime state derived from the parser's
    ; semantic AST.  Concrete .mm0 files remain parser input; no declaration
    ; below is specialized into generated target code.
    (rule mm0-delimiter-class-merge-left-left-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathLeftDelimiterV1 MM0MathLeftDelimiterV1
          MM0MathLeftDelimiterV1))
      (body))
    (rule mm0-delimiter-class-merge-left-right-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathLeftDelimiterV1 MM0MathRightDelimiterV1
          MM0MathBothDelimiterV1))
      (body))
    (rule mm0-delimiter-class-merge-left-both-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathLeftDelimiterV1 MM0MathBothDelimiterV1
          MM0MathBothDelimiterV1))
      (body))
    (rule mm0-delimiter-class-merge-right-left-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathRightDelimiterV1 MM0MathLeftDelimiterV1
          MM0MathBothDelimiterV1))
      (body))
    (rule mm0-delimiter-class-merge-right-right-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathRightDelimiterV1 MM0MathRightDelimiterV1
          MM0MathRightDelimiterV1))
      (body))
    (rule mm0-delimiter-class-merge-right-both-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathRightDelimiterV1 MM0MathBothDelimiterV1
          MM0MathBothDelimiterV1))
      (body))
    (rule mm0-delimiter-class-merge-both-left-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathBothDelimiterV1 MM0MathLeftDelimiterV1
          MM0MathBothDelimiterV1))
      (body))
    (rule mm0-delimiter-class-merge-both-right-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathBothDelimiterV1 MM0MathRightDelimiterV1
          MM0MathBothDelimiterV1))
      (body))
    (rule mm0-delimiter-class-merge-both-both-v1
      (head
        (MM0DelimiterClassMergeV1
          MM0MathBothDelimiterV1 MM0MathBothDelimiterV1
          MM0MathBothDelimiterV1))
      (body))

    (rule mm0-delimiter-class-put-new-v1
      (head
        (MM0DelimiterClassPutV1
          ?scalar ?addition MM0MathClassTableNilV1
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?scalar ?addition)
            MM0MathClassTableNilV1)))
      (body))
    (rule mm0-delimiter-class-put-merge-v1
      (head
        (MM0DelimiterClassPutV1
          ?scalar ?addition
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?scalar ?existing) ?rest)
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?scalar ?merged) ?rest)))
      (body
        (MM0DelimiterClassMergeV1 ?existing ?addition ?merged)))
    (rule mm0-delimiter-class-put-tail-v1
      (head
        (MM0DelimiterClassPutV1
          ?scalar ?addition
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?other ?class) ?rest)
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1 ?other ?class) ?updated)))
      (body
        (different ?scalar ?other)
        (MM0DelimiterClassPutV1
          ?scalar ?addition ?rest ?updated)))

    ; A delimiter declaration contains a space/newline-separated list of
    ; single Unicode scalars. Adjacent non-whitespace scalars therefore fail
    ; closed instead of being misread as several one-character delimiters.
    (rule mm0-delimiter-characters-fold-nil-v1
      (head
        (MM0DelimiterCharactersFoldV1
          nil ?class ?table ?table))
      (body))
    (rule mm0-delimiter-characters-fold-space-v1
      (head
        (MM0DelimiterCharactersFoldV1
          (cons (cp 32) ?rest) ?class ?before ?after))
      (body
        (MM0DelimiterCharactersFoldV1
          ?rest ?class ?before ?after)))
    (rule mm0-delimiter-characters-fold-newline-v1
      (head
        (MM0DelimiterCharactersFoldV1
          (cons (cp 10) ?rest) ?class ?before ?after))
      (body
        (MM0DelimiterCharactersFoldV1
          ?rest ?class ?before ?after)))
    (rule mm0-delimiter-characters-fold-scalar-v1
      (head
        (MM0DelimiterCharactersFoldV1
          (cons ?scalar ?rest) ?class ?before ?after))
      (body
        (different ?scalar (cp 32))
        (different ?scalar (cp 10))
        (MM0DelimiterScalarV1 ?scalar)
        (MM0DelimiterClassPutV1
          ?scalar ?class ?before ?next)
        (MM0DelimiterCharactersAfterScalarV1
          ?rest ?class ?next ?after)))
    (rule mm0-delimiter-characters-after-scalar-nil-v1
      (head
        (MM0DelimiterCharactersAfterScalarV1
          nil ?class ?table ?table))
      (body))
    (rule mm0-delimiter-characters-after-scalar-space-v1
      (head
        (MM0DelimiterCharactersAfterScalarV1
          (cons (cp 32) ?rest) ?class ?before ?after))
      (body
        (MM0DelimiterCharactersFoldV1
          ?rest ?class ?before ?after)))
    (rule mm0-delimiter-characters-after-scalar-newline-v1
      (head
        (MM0DelimiterCharactersAfterScalarV1
          (cons (cp 10) ?rest) ?class ?before ?after))
      (body
        (MM0DelimiterCharactersFoldV1
          ?rest ?class ?before ?after)))

    (rule mm0-delimiter-environment-fold-nil-v1
      (head
        (MM0DelimiterEnvironmentFoldV1 nil ?table ?table))
      (body))
    (rule mm0-delimiter-environment-fold-both-v1
      (head
        (MM0DelimiterEnvironmentFoldV1
          (cons
            (node mm0-delimiter-statement
              (pair (node mm0-math-string ?both) none))
            ?rest)
          ?before ?after))
      (body
        (MM0DelimiterCharactersFoldV1
          ?both MM0MathBothDelimiterV1 ?before ?next)
        (MM0DelimiterEnvironmentFoldV1 ?rest ?next ?after)))
    (rule mm0-delimiter-environment-fold-left-right-v1
      (head
        (MM0DelimiterEnvironmentFoldV1
          (cons
            (node mm0-delimiter-statement
              (pair
                (node mm0-math-string ?left)
                (some (node mm0-math-string ?right))))
            ?rest)
          ?before ?after))
      (body
        (MM0DelimiterCharactersFoldV1
          ?left MM0MathLeftDelimiterV1 ?before ?middle)
        (MM0DelimiterCharactersFoldV1
          ?right MM0MathRightDelimiterV1 ?middle ?next)
        (MM0DelimiterEnvironmentFoldV1 ?rest ?next ?after)))
    (rule mm0-delimiter-environment-fold-other-v1
      (head
        (MM0DelimiterEnvironmentFoldV1
          (cons (node ?kind ?payload) ?rest) ?before ?after))
      (body
        (different ?kind mm0-delimiter-statement)
        (MM0DelimiterEnvironmentFoldV1 ?rest ?before ?after)))

    (rule mm0-delimiter-environment-from-ast-v1
      (head
        (MM0DelimiterEnvironmentFromAstV1
          (node mm0-file ?statements) ?environment))
      (body
        (MM0DelimiterEnvironmentFoldV1
          ?statements
          (MM0MathClassTableConsV1
            (MM0MathClassEntryV1
              (cp 32) MM0MathWhitespaceV1)
            (MM0MathClassTableConsV1
              (MM0MathClassEntryV1
                (cp 10) MM0MathWhitespaceV1)
              MM0MathClassTableNilV1))
          ?environment)))

    ; This is the direct composition boundary: syntax-state extraction and
    ; secondary lexing remain separately authored GSLTs and compose by their
    ; shared delimiter environment value.
    (rule mm0-live-delimiter-lex-v1
      (head
        (MM0LiveDelimiterLexV1 ?ast ?scalars ?tokens))
      (body
        (MM0DelimiterEnvironmentFromAstV1 ?ast ?environment)
        (MM0SecondaryLexV1 ?environment ?scalars ?tokens)))))
