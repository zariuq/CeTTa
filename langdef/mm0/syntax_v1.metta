; Primary textual syntax for the complete outer .mm0 language. Mathematical
; strings remain opaque here and are intentionally handed to the separately
; generated, notation-dependent secondary lexer and precedence parser.
(gslt-presentation-v1 MM0TextualSyntaxV1
  (signature)
  (equations)
  (rewrites
    ; Exact scalar complements. ParserPack compiles these `different` premises
    ; to Unicode-scalar interval complements; no host lexer case is involved.
    (rule member-mm0-non-dollar
      (head (member mm0-non-dollar ?codepoint))
      (body (different ?codepoint (cp 36))))
    (rule member-mm0-non-newline
      (head (member mm0-non-newline ?codepoint))
      (body (different ?codepoint (cp 10))))
    (rule member-mm0-non-digit
      (head (member mm0-non-digit ?codepoint))
      (body
        (different ?codepoint (cp 48))
        (different ?codepoint (cp 49))
        (different ?codepoint (cp 50))
        (different ?codepoint (cp 51))
        (different ?codepoint (cp 52))
        (different ?codepoint (cp 53))
        (different ?codepoint (cp 54))
        (different ?codepoint (cp 55))
        (different ?codepoint (cp 56))
        (different ?codepoint (cp 57))))
    (rule member-mm0-non-identifier-rest
      (head (member mm0-non-identifier-rest ?codepoint))
      (body
        (different ?codepoint (cp 48))
        (different ?codepoint (cp 49))
        (different ?codepoint (cp 50))
        (different ?codepoint (cp 51))
        (different ?codepoint (cp 52))
        (different ?codepoint (cp 53))
        (different ?codepoint (cp 54))
        (different ?codepoint (cp 55))
        (different ?codepoint (cp 56))
        (different ?codepoint (cp 57))
        (different ?codepoint (cp 65))
        (different ?codepoint (cp 66))
        (different ?codepoint (cp 67))
        (different ?codepoint (cp 68))
        (different ?codepoint (cp 69))
        (different ?codepoint (cp 70))
        (different ?codepoint (cp 71))
        (different ?codepoint (cp 72))
        (different ?codepoint (cp 73))
        (different ?codepoint (cp 74))
        (different ?codepoint (cp 75))
        (different ?codepoint (cp 76))
        (different ?codepoint (cp 77))
        (different ?codepoint (cp 78))
        (different ?codepoint (cp 79))
        (different ?codepoint (cp 80))
        (different ?codepoint (cp 81))
        (different ?codepoint (cp 82))
        (different ?codepoint (cp 83))
        (different ?codepoint (cp 84))
        (different ?codepoint (cp 85))
        (different ?codepoint (cp 86))
        (different ?codepoint (cp 87))
        (different ?codepoint (cp 88))
        (different ?codepoint (cp 89))
        (different ?codepoint (cp 90))
        (different ?codepoint (cp 95))
        (different ?codepoint (cp 97))
        (different ?codepoint (cp 98))
        (different ?codepoint (cp 99))
        (different ?codepoint (cp 100))
        (different ?codepoint (cp 101))
        (different ?codepoint (cp 102))
        (different ?codepoint (cp 103))
        (different ?codepoint (cp 104))
        (different ?codepoint (cp 105))
        (different ?codepoint (cp 106))
        (different ?codepoint (cp 107))
        (different ?codepoint (cp 108))
        (different ?codepoint (cp 109))
        (different ?codepoint (cp 110))
        (different ?codepoint (cp 111))
        (different ?codepoint (cp 112))
        (different ?codepoint (cp 113))
        (different ?codepoint (cp 114))
        (different ?codepoint (cp 115))
        (different ?codepoint (cp 116))
        (different ?codepoint (cp 117))
        (different ?codepoint (cp 118))
        (different ?codepoint (cp 119))
        (different ?codepoint (cp 120))
        (different ?codepoint (cp 121))
        (different ?codepoint (cp 122))))

    (rule member-mm0-white-space
      (head (member mm0-whitechar (cp 32))) (body))
    (rule member-mm0-white-newline
      (head (member mm0-whitechar (cp 10))) (body))
    (rule member-mm0-underscore
      (head (member mm0-underscore (cp 95))) (body))

    (rule def-mm0-line-comment
      (head
        (definition mm0-line-comment
          (node mm0-line-comment
            (right
              (lit (cons (cp 45) (cons (cp 45) nil)))
              (left
                (star (class mm0-non-newline))
                (char (cp 10)))))))
      (body))
    (rule def-mm0-layout-unit
      (head
        (definition mm0-layout-unit
          (alt (class mm0-whitechar) (ref mm0-line-comment))))
      (body))
    (rule def-mm0-skip
      (head (definition mm0-skip (star (ref mm0-layout-unit))))
      (body))
    (rule def-mm0-identifier-rest-class
      (head
        (definition mm0-identifier-rest-char
          (class
            (c-union ascii-lower
              (c-union ascii-upper
                (c-union ascii-digit mm0-underscore))))))
      (body))
    (rule def-mm0-identifier-boundary
      (head
        (definition mm0-identifier-boundary
          (alt (class mm0-non-identifier-rest) eof)))
      (body))
    (rule def-mm0-number-boundary
      (head
        (definition mm0-number-boundary
          (alt (class mm0-non-digit) eof)))
      (body))
    (rule def-mm0-identifier-raw-alpha
      (head
        (definition mm0-identifier-raw-alpha
          (node mm0-identifier
            (seq
              (class (c-union ascii-lower ascii-upper))
              (star (ref mm0-identifier-rest-char))))))
      (body))
    (rule def-mm0-identifier-raw-underscore
      (head
        (definition mm0-identifier-raw-underscore
          (node mm0-identifier
            (seq
              (class mm0-underscore)
              (plus (ref mm0-identifier-rest-char))))))
      (body))
    (rule def-mm0-identifier-raw
      (head
        (definition mm0-identifier-raw
          (alt
            (ref mm0-identifier-raw-alpha)
            (ref mm0-identifier-raw-underscore))))
      (body))
    (rule def-mm0-identifier-lexeme
      (head
        (definition mm0-identifier-lexeme
          (left
            (ref mm0-identifier-raw)
            (peek (ref mm0-identifier-boundary)))))
      (body))
    (rule def-mm0-identifier
      (head
        (definition mm0-identifier
          (left (ref mm0-identifier-lexeme) (ref mm0-skip))))
      (body))
    (rule def-mm0-anonymous-lexeme
      (head
        (definition mm0-anonymous-lexeme
          (left
            (node mm0-anonymous (char (cp 95)))
            (peek (ref mm0-identifier-boundary)))))
      (body))
    (rule def-mm0-anonymous
      (head
        (definition mm0-anonymous
          (left (ref mm0-anonymous-lexeme) (ref mm0-skip))))
      (body))
    (rule def-mm0-identifier-or-anonymous
      (head
        (definition mm0-identifier-or-anonymous
          (alt (ref mm0-identifier) (ref mm0-anonymous))))
      (body))

    (rule def-mm0-number-raw
      (head
        (definition mm0-number-raw
          (node mm0-number
            (alt
              (char (cp 48))
              (seq
                (class ascii-nonzero-digit)
                (star (class ascii-digit)))))))
      (body))
    (rule def-mm0-number-lexeme
      (head
        (definition mm0-number-lexeme
          (left
            (ref mm0-number-raw)
            (peek (ref mm0-number-boundary)))))
      (body))
    (rule def-mm0-number
      (head
        (definition mm0-number
          (left (ref mm0-number-lexeme) (ref mm0-skip))))
      (body))
    (rule def-mm0-math-string-lexeme
      (head
        (definition mm0-math-string-lexeme
          (node mm0-math-string
            (right
              (char (cp 36))
              (left
                (star (class mm0-non-dollar))
                (char (cp 36)))))))
      (body))
    (rule def-mm0-math-string
      (head
        (definition mm0-math-string
          (left (ref mm0-math-string-lexeme) (ref mm0-skip))))
      (body))

    ; Single-character symbols.
    (rule def-mm0-star
      (head (definition mm0-star (left (char (cp 42)) (ref mm0-skip))))
      (body))
    (rule def-mm0-dot
      (head (definition mm0-dot (left (char (cp 46)) (ref mm0-skip))))
      (body))
    (rule def-mm0-colon
      (head (definition mm0-colon (left (char (cp 58)) (ref mm0-skip))))
      (body))
    (rule def-mm0-semicolon
      (head (definition mm0-semicolon (left (char (cp 59)) (ref mm0-skip))))
      (body))
    (rule def-mm0-left-paren
      (head (definition mm0-left-paren (left (char (cp 40)) (ref mm0-skip))))
      (body))
    (rule def-mm0-right-paren
      (head (definition mm0-right-paren (left (char (cp 41)) (ref mm0-skip))))
      (body))
    (rule def-mm0-greater
      (head (definition mm0-greater (left (char (cp 62)) (ref mm0-skip))))
      (body))
    (rule def-mm0-left-brace
      (head (definition mm0-left-brace (left (char (cp 123)) (ref mm0-skip))))
      (body))
    (rule def-mm0-right-brace
      (head (definition mm0-right-brace (left (char (cp 125)) (ref mm0-skip))))
      (body))
    (rule def-mm0-equals
      (head (definition mm0-equals (left (char (cp 61)) (ref mm0-skip))))
      (body))

    ; Pseudo-keywords are identifiers in the lexical grammar. Each lexeme
    ; carries its own positive boundary guard so a keyword prefix cannot split
    ; a longer identifier. Layout is consumed only after that guard succeeds.
    (rule def-mm0-kw-axiom-lexeme
      (head (definition mm0-kw-axiom-lexeme (left (lit (cons (cp 97) (cons (cp 120) (cons (cp 105) (cons (cp 111) (cons (cp 109) nil)))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-axiom
      (head (definition mm0-kw-axiom (left (ref mm0-kw-axiom-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-coercion-lexeme
      (head (definition mm0-kw-coercion-lexeme (left (lit (cons (cp 99) (cons (cp 111) (cons (cp 101) (cons (cp 114) (cons (cp 99) (cons (cp 105) (cons (cp 111) (cons (cp 110) nil))))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-coercion
      (head (definition mm0-kw-coercion (left (ref mm0-kw-coercion-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-def-lexeme
      (head (definition mm0-kw-def-lexeme (left (lit (cons (cp 100) (cons (cp 101) (cons (cp 102) nil)))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-def
      (head (definition mm0-kw-def (left (ref mm0-kw-def-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-delimiter-lexeme
      (head (definition mm0-kw-delimiter-lexeme (left (lit (cons (cp 100) (cons (cp 101) (cons (cp 108) (cons (cp 105) (cons (cp 109) (cons (cp 105) (cons (cp 116) (cons (cp 101) (cons (cp 114) nil)))))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-delimiter
      (head (definition mm0-kw-delimiter (left (ref mm0-kw-delimiter-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-free-lexeme
      (head (definition mm0-kw-free-lexeme (left (lit (cons (cp 102) (cons (cp 114) (cons (cp 101) (cons (cp 101) nil))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-free
      (head (definition mm0-kw-free (left (ref mm0-kw-free-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-infixl-lexeme
      (head (definition mm0-kw-infixl-lexeme (left (lit (cons (cp 105) (cons (cp 110) (cons (cp 102) (cons (cp 105) (cons (cp 120) (cons (cp 108) nil))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-infixl
      (head (definition mm0-kw-infixl (left (ref mm0-kw-infixl-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-infixr-lexeme
      (head (definition mm0-kw-infixr-lexeme (left (lit (cons (cp 105) (cons (cp 110) (cons (cp 102) (cons (cp 105) (cons (cp 120) (cons (cp 114) nil))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-infixr
      (head (definition mm0-kw-infixr (left (ref mm0-kw-infixr-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-input-lexeme
      (head (definition mm0-kw-input-lexeme (left (lit (cons (cp 105) (cons (cp 110) (cons (cp 112) (cons (cp 117) (cons (cp 116) nil)))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-input
      (head (definition mm0-kw-input (left (ref mm0-kw-input-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-max-lexeme
      (head (definition mm0-kw-max-lexeme (left (lit (cons (cp 109) (cons (cp 97) (cons (cp 120) nil)))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-max
      (head (definition mm0-kw-max (left (ref mm0-kw-max-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-notation-lexeme
      (head (definition mm0-kw-notation-lexeme (left (lit (cons (cp 110) (cons (cp 111) (cons (cp 116) (cons (cp 97) (cons (cp 116) (cons (cp 105) (cons (cp 111) (cons (cp 110) nil))))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-notation
      (head (definition mm0-kw-notation (left (ref mm0-kw-notation-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-output-lexeme
      (head (definition mm0-kw-output-lexeme (left (lit (cons (cp 111) (cons (cp 117) (cons (cp 116) (cons (cp 112) (cons (cp 117) (cons (cp 116) nil))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-output
      (head (definition mm0-kw-output (left (ref mm0-kw-output-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-prec-lexeme
      (head (definition mm0-kw-prec-lexeme (left (lit (cons (cp 112) (cons (cp 114) (cons (cp 101) (cons (cp 99) nil))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-prec
      (head (definition mm0-kw-prec (left (ref mm0-kw-prec-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-prefix-lexeme
      (head (definition mm0-kw-prefix-lexeme (left (lit (cons (cp 112) (cons (cp 114) (cons (cp 101) (cons (cp 102) (cons (cp 105) (cons (cp 120) nil))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-prefix
      (head (definition mm0-kw-prefix (left (ref mm0-kw-prefix-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-provable-lexeme
      (head (definition mm0-kw-provable-lexeme (left (lit (cons (cp 112) (cons (cp 114) (cons (cp 111) (cons (cp 118) (cons (cp 97) (cons (cp 98) (cons (cp 108) (cons (cp 101) nil))))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-provable
      (head (definition mm0-kw-provable (left (ref mm0-kw-provable-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-pure-lexeme
      (head (definition mm0-kw-pure-lexeme (left (lit (cons (cp 112) (cons (cp 117) (cons (cp 114) (cons (cp 101) nil))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-pure
      (head (definition mm0-kw-pure (left (ref mm0-kw-pure-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-sort-lexeme
      (head (definition mm0-kw-sort-lexeme (left (lit (cons (cp 115) (cons (cp 111) (cons (cp 114) (cons (cp 116) nil))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-sort
      (head (definition mm0-kw-sort (left (ref mm0-kw-sort-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-strict-lexeme
      (head (definition mm0-kw-strict-lexeme (left (lit (cons (cp 115) (cons (cp 116) (cons (cp 114) (cons (cp 105) (cons (cp 99) (cons (cp 116) nil))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-strict
      (head (definition mm0-kw-strict (left (ref mm0-kw-strict-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-term-lexeme
      (head (definition mm0-kw-term-lexeme (left (lit (cons (cp 116) (cons (cp 101) (cons (cp 114) (cons (cp 109) nil))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-term
      (head (definition mm0-kw-term (left (ref mm0-kw-term-lexeme) (ref mm0-skip)))) (body))
    (rule def-mm0-kw-theorem-lexeme
      (head (definition mm0-kw-theorem-lexeme (left (lit (cons (cp 116) (cons (cp 104) (cons (cp 101) (cons (cp 111) (cons (cp 114) (cons (cp 101) (cons (cp 109) nil)))))))) (peek (ref mm0-identifier-boundary))))) (body))
    (rule def-mm0-kw-theorem
      (head (definition mm0-kw-theorem (left (ref mm0-kw-theorem-lexeme) (ref mm0-skip)))) (body))

    ; Shared type and binder forms.
    (rule def-mm0-type
      (head
        (definition mm0-type
          (node mm0-type
            (seq (ref mm0-identifier) (star (ref mm0-identifier))))))
      (body))
    (rule def-mm0-type-binder-bound
      (head
        (definition mm0-type-binder-bound
          (node mm0-type-binder-bound
            (right (ref mm0-left-brace)
              (left
                (seq
                  (star (ref mm0-identifier))
                  (right (ref mm0-colon) (ref mm0-type)))
                (ref mm0-right-brace))))))
      (body))
    (rule def-mm0-type-binder-regular
      (head
        (definition mm0-type-binder-regular
          (node mm0-type-binder-regular
            (right (ref mm0-left-paren)
              (left
                (seq
                  (star (ref mm0-identifier-or-anonymous))
                  (right (ref mm0-colon) (ref mm0-type)))
                (ref mm0-right-paren))))))
      (body))
    (rule def-mm0-type-binder
      (head
        (definition mm0-type-binder
          (alt
            (ref mm0-type-binder-bound)
            (ref mm0-type-binder-regular))))
      (body))
    (rule def-mm0-arrow-type
      (head
        (definition mm0-arrow-type
          (node mm0-arrow-type
            (seq
              (ref mm0-type)
              (opt (right (ref mm0-greater) (ref mm0-arrow-type)))))))
      (body))

    ; Sort declarations preserve the specification's modifier order.
    (rule def-mm0-sort-statement
      (head
        (definition mm0-sort-statement
          (node mm0-sort-statement
            (seq
              (opt (ref mm0-kw-pure))
              (seq
                (opt (ref mm0-kw-strict))
                (seq
                  (opt (ref mm0-kw-provable))
                  (seq
                    (opt (ref mm0-kw-free))
                    (right (ref mm0-kw-sort)
                      (left
                        (ref mm0-identifier)
                        (ref mm0-semicolon))))))))))
      (body))

    (rule def-mm0-term-statement
      (head
        (definition mm0-term-statement
          (node mm0-term-statement
            (right (ref mm0-kw-term)
              (seq
                (ref mm0-identifier)
                (seq
                  (star (ref mm0-type-binder))
                  (right (ref mm0-colon)
                    (left
                      (ref mm0-arrow-type)
                      (ref mm0-semicolon)))))))))
      (body))

    ; Assertion binders and formula arrows.
    (rule def-mm0-formula-binder-bound
      (head
        (definition mm0-formula-binder-bound
          (node mm0-formula-binder-bound
            (right (ref mm0-left-brace)
              (left
                (seq
                  (star (ref mm0-identifier))
                  (right (ref mm0-colon) (ref mm0-type)))
                (ref mm0-right-brace))))))
      (body))
    (rule def-mm0-formula-binder-regular
      (head
        (definition mm0-formula-binder-regular
          (node mm0-formula-binder-regular
            (right (ref mm0-left-paren)
              (left
                (seq
                  (star (ref mm0-identifier-or-anonymous))
                  (right (ref mm0-colon)
                    (alt (ref mm0-type) (ref mm0-math-string))))
                (ref mm0-right-paren))))))
      (body))
    (rule def-mm0-formula-binder
      (head
        (definition mm0-formula-binder
          (alt
            (ref mm0-formula-binder-bound)
            (ref mm0-formula-binder-regular))))
      (body))
    (rule def-mm0-formula-arrow-unit
      (head
        (definition mm0-formula-arrow-unit
          (alt (ref mm0-type) (ref mm0-math-string))))
      (body))
    (rule def-mm0-formula-arrow-type
      (head
        (definition mm0-formula-arrow-type
          (node mm0-formula-arrow-type
            (seq
              (ref mm0-formula-arrow-unit)
              (opt
                (right
                  (ref mm0-greater)
                  (ref mm0-formula-arrow-type)))))))
      (body))
    (rule def-mm0-assertion-kind
      (head
        (definition mm0-assertion-kind
          (alt
            (node mm0-axiom-kind (ref mm0-kw-axiom))
            (node mm0-theorem-kind (ref mm0-kw-theorem)))))
      (body))
    (rule def-mm0-assertion-statement
      (head
        (definition mm0-assertion-statement
          (node mm0-assertion-statement
            (seq (ref mm0-assertion-kind)
              (seq
                (ref mm0-identifier)
                (seq
                  (star (ref mm0-formula-binder))
                  (right (ref mm0-colon)
                    (left
                      (ref mm0-formula-arrow-type)
                      (ref mm0-semicolon)))))))))
      (body))

    ; Definitions add dot-prefixed dummy variables and an optional definiens.
    (rule def-mm0-dummy-identifier-dot
      (head
        (definition mm0-dummy-identifier-dot
          (node mm0-dummy-identifier-dot
            (right (ref mm0-dot) (ref mm0-identifier)))))
      (body))
    (rule def-mm0-dummy-identifier
      (head
        (definition mm0-dummy-identifier
          (alt
            (ref mm0-dummy-identifier-dot)
            (ref mm0-identifier-or-anonymous))))
      (body))
    (rule def-mm0-dummy-binder-bound
      (head
        (definition mm0-dummy-binder-bound
          (node mm0-dummy-binder-bound
            (right (ref mm0-left-brace)
              (left
                (seq
                  (star (ref mm0-dummy-identifier))
                  (right (ref mm0-colon) (ref mm0-type)))
                (ref mm0-right-brace))))))
      (body))
    (rule def-mm0-dummy-binder-regular
      (head
        (definition mm0-dummy-binder-regular
          (node mm0-dummy-binder-regular
            (right (ref mm0-left-paren)
              (left
                (seq
                  (star (ref mm0-dummy-identifier))
                  (right (ref mm0-colon) (ref mm0-type)))
                (ref mm0-right-paren))))))
      (body))
    (rule def-mm0-dummy-binder
      (head
        (definition mm0-dummy-binder
          (alt
            (ref mm0-dummy-binder-bound)
            (ref mm0-dummy-binder-regular))))
      (body))
    (rule def-mm0-definition-value
      (head
        (definition mm0-definition-value
          (right (ref mm0-equals) (ref mm0-math-string))))
      (body))
    (rule def-mm0-definition-statement
      (head
        (definition mm0-definition-statement
          (node mm0-definition-statement
            (right (ref mm0-kw-def)
              (seq
                (ref mm0-identifier)
                (seq
                  (star (ref mm0-dummy-binder))
                  (right (ref mm0-colon)
                    (seq
                      (ref mm0-type)
                      (left
                        (opt (ref mm0-definition-value))
                        (ref mm0-semicolon))))))))))
      (body))

    ; Notation declarations.
    (rule def-mm0-precedence
      (head
        (definition mm0-precedence
          (alt (ref mm0-number) (ref mm0-kw-max))))
      (body))
    (rule def-mm0-delimiter-statement
      (head
        (definition mm0-delimiter-statement
          (node mm0-delimiter-statement
            (right (ref mm0-kw-delimiter)
              (seq
                (ref mm0-math-string)
                (left
                  (opt (ref mm0-math-string))
                  (ref mm0-semicolon)))))))
      (body))
    (rule def-mm0-simple-notation-kind
      (head
        (definition mm0-simple-notation-kind
          (alt
            (node mm0-infixl-kind (ref mm0-kw-infixl))
            (alt
              (node mm0-infixr-kind (ref mm0-kw-infixr))
              (node mm0-prefix-kind (ref mm0-kw-prefix))))))
      (body))
    (rule def-mm0-simple-notation-statement
      (head
        (definition mm0-simple-notation-statement
          (node mm0-simple-notation-statement
            (seq (ref mm0-simple-notation-kind)
              (seq
                (ref mm0-identifier)
                (right (ref mm0-colon)
                  (seq
                    (ref mm0-math-string)
                    (right (ref mm0-kw-prec)
                      (left
                        (ref mm0-precedence)
                        (ref mm0-semicolon))))))))))
      (body))
    (rule def-mm0-coercion-statement
      (head
        (definition mm0-coercion-statement
          (node mm0-coercion-statement
            (right (ref mm0-kw-coercion)
              (seq
                (ref mm0-identifier)
                (right (ref mm0-colon)
                  (seq
                    (ref mm0-identifier)
                    (right (ref mm0-greater)
                      (left
                        (ref mm0-identifier)
                        (ref mm0-semicolon))))))))))
      (body))
    (rule def-mm0-precedence-constant
      (head
        (definition mm0-precedence-constant
          (node mm0-precedence-constant
            (right (ref mm0-left-paren)
              (left
                (seq
                  (ref mm0-math-string)
                  (right (ref mm0-colon) (ref mm0-precedence)))
                (ref mm0-right-paren))))))
      (body))
    (rule def-mm0-notation-literal
      (head
        (definition mm0-notation-literal
          (alt
            (ref mm0-precedence-constant)
            (ref mm0-identifier))))
      (body))
    (rule def-mm0-general-notation-statement
      (head
        (definition mm0-general-notation-statement
          (node mm0-general-notation-statement
            (right (ref mm0-kw-notation)
              (seq
                (ref mm0-identifier)
                (seq
                  (star (ref mm0-type-binder))
                  (right (ref mm0-colon)
                    (seq
                      (ref mm0-type)
                      (right (ref mm0-equals)
                        (seq
                          (ref mm0-precedence-constant)
                          (left
                            (star (ref mm0-notation-literal))
                            (ref mm0-semicolon))))))))))))
      (body))
    (rule def-mm0-notation-statement
      (head
        (definition mm0-notation-statement
          (alt
            (ref mm0-delimiter-statement)
            (alt
              (ref mm0-simple-notation-statement)
              (alt
                (ref mm0-coercion-statement)
                (ref mm0-general-notation-statement))))))
      (body))

    ; Optional implementation-defined input/output syntax is represented
    ; completely; execution policy belongs to the later semantic package.
    (rule def-mm0-input-output-item
      (head
        (definition mm0-input-output-item
          (alt (ref mm0-identifier) (ref mm0-math-string))))
      (body))
    (rule def-mm0-input-statement
      (head
        (definition mm0-input-statement
          (node mm0-input-statement
            (right (ref mm0-kw-input)
              (seq
                (ref mm0-identifier)
                (right (ref mm0-colon)
                  (left
                    (star (ref mm0-input-output-item))
                    (ref mm0-semicolon))))))))
      (body))
    (rule def-mm0-output-statement
      (head
        (definition mm0-output-statement
          (node mm0-output-statement
            (right (ref mm0-kw-output)
              (seq
                (ref mm0-identifier)
                (right (ref mm0-colon)
                  (left
                    (star (ref mm0-input-output-item))
                    (ref mm0-semicolon))))))))
      (body))
    (rule def-mm0-input-output-statement
      (head
        (definition mm0-input-output-statement
          (alt
            (ref mm0-input-statement)
            (ref mm0-output-statement))))
      (body))

    (rule def-mm0-statement
      (head
        (definition mm0-statement
          (alt
            (ref mm0-sort-statement)
            (alt
              (ref mm0-term-statement)
              (alt
                (ref mm0-assertion-statement)
                (alt
                  (ref mm0-definition-statement)
                  (alt
                    (ref mm0-notation-statement)
                    (ref mm0-input-output-statement))))))))
      (body))
    (rule def-mm0-file
      (head
        (definition mm0-file
          (node mm0-file
            (right
              (ref mm0-skip)
              (left (star (ref mm0-statement)) eof)))))
      (body))
  ))
