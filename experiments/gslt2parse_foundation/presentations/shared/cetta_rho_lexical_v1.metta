; Shared scannerless layout, identifier, and punctuation layer for CeTTa rho.
(gslt-presentation-v1 CeTTaRhoLexicalV1
  (signature)
  (equations)
  (rewrites
    (rule def-cetta-rho-line-comment
      (head (definition cetta-rho-line-comment
        (node comment
          (right
            (lit (cons (cp 47) (cons (cp 47) nil)))
            (alt
              (until (class cetta-rho-newline-scalar) (class cetta-rho-non-newline-scalar))
              (until eof (class cetta-rho-non-newline-scalar)))))))
      (body))
    (rule def-cetta-rho-block-unit
      (head (definition cetta-rho-block-unit
        (alt
          (class cetta-rho-block-non-star-scalar)
          (seq (char (cp 42)) (class cetta-rho-block-non-slash-scalar)))))
      (body))
    (rule def-cetta-rho-block-comment
      (head (definition cetta-rho-block-comment
        (node comment
          (right
            (lit (cons (cp 47) (cons (cp 42) nil)))
            (until
              (lit (cons (cp 42) (cons (cp 47) nil)))
              (ref cetta-rho-block-unit))))))
      (body))
    (rule def-cetta-rho-layout-unit
      (head (definition cetta-rho-layout-unit
        (alt
          (class cetta-rho-whitespace-scalar)
          (alt (ref cetta-rho-line-comment) (ref cetta-rho-block-comment)))))
      (body))
    (rule def-cetta-rho-skip
      (head (definition cetta-rho-skip (star (ref cetta-rho-layout-unit))))
      (body))

    (rule def-cetta-rho-ident-start
      (head (definition cetta-rho-ident-start
        (class (c-union ascii-lower (c-union ascii-upper underscore)))))
      (body))
    (rule def-cetta-rho-ident-tail
      (head (definition cetta-rho-ident-tail
        (alt
          (ref cetta-rho-ident-start)
          (alt
            (class ascii-digit)
            (alt (char (cp 45)) (char (cp 63)))))))
      (body))
    (rule def-cetta-rho-ident-start-not-f
      (head (definition cetta-rho-ident-start-not-f
        (alt
          (class cetta-rho-ascii-lower-not-f)
          (alt (class ascii-upper) (class underscore)))))
      (body))
    (rule def-cetta-rho-ident-tail-not-o
      (head (definition cetta-rho-ident-tail-not-o
        (alt
          (class cetta-rho-ascii-lower-not-o)
          (alt
            (class ascii-upper)
            (alt
              (class underscore)
              (alt
                (class ascii-digit)
                (alt (char (cp 45)) (char (cp 63)))))))))
      (body))
    (rule def-cetta-rho-ident-tail-not-r
      (head (definition cetta-rho-ident-tail-not-r
        (alt
          (class cetta-rho-ascii-lower-not-r)
          (alt
            (class ascii-upper)
            (alt
              (class underscore)
              (alt
                (class ascii-digit)
                (alt (char (cp 45)) (char (cp 63)))))))))
      (body))
    (rule def-cetta-rho-ident-tail-not-question
      (head (definition cetta-rho-ident-tail-not-question
        (alt
          (ref cetta-rho-ident-start)
          (alt (class ascii-digit) (char (cp 45))))))
      (body))

    (rule def-cetta-rho-ident-raw
      (head (definition cetta-rho-ident-raw
        (seq
          (ref cetta-rho-ident-start)
          (star (ref cetta-rho-ident-tail)))))
      (body))
    (rule def-cetta-rho-ident
      (head (definition cetta-rho-ident
        (node identifier (left (ref cetta-rho-ident-raw) (ref cetta-rho-skip)))))
      (body))

    ; This regular sublanguage mirrors keyword priority in rp_parse_proc_atom.
    (rule def-cetta-rho-process-leading-ident-raw
      (head (definition cetta-rho-process-leading-ident-raw
        (alt
          (seq
            (ref cetta-rho-ident-start-not-f)
            (star (ref cetta-rho-ident-tail)))
          (right
            (char (cp 102))
            (alt
              (eps ident-end)
              (alt
                (seq
                  (ref cetta-rho-ident-tail-not-o)
                  (star (ref cetta-rho-ident-tail)))
                (right
                  (char (cp 111))
                  (alt
                    (eps ident-end)
                    (alt
                      (seq
                        (ref cetta-rho-ident-tail-not-r)
                        (star (ref cetta-rho-ident-tail)))
                      (right
                        (char (cp 114))
                        (seq
                          (ref cetta-rho-ident-tail-not-question)
                          (star (ref cetta-rho-ident-tail)))))))))))))
      (body))
    (rule def-cetta-rho-process-leading-ident
      (head (definition cetta-rho-process-leading-ident
        (node identifier
          (left
            (ref cetta-rho-process-leading-ident-raw)
            (ref cetta-rho-skip)))))
      (body))

    (rule def-cetta-rho-k-at (head (definition cetta-rho-k-at (left (char (cp 64)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-bang (head (definition cetta-rho-k-bang (left (char (cp 33)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-colon (head (definition cetta-rho-k-colon (left (char (cp 58)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-dollar (head (definition cetta-rho-k-dollar (left (char (cp 36)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-empty (head (definition cetta-rho-k-empty (left (lit (cons (cp 40) (cons (cp 41) nil))) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-for (head (definition cetta-rho-k-for (left (lit (cons (cp 102) (cons (cp 111) (cons (cp 114) nil)))) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-larrow (head (definition cetta-rho-k-larrow (left (lit (cons (cp 60) (cons (cp 45) nil))) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-lbrace (head (definition cetta-rho-k-lbrace (left (char (cp 123)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-lparen (head (definition cetta-rho-k-lparen (left (char (cp 40)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-par (head (definition cetta-rho-k-par (left (char (cp 124)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-purse (head (definition cetta-rho-k-purse (left (lit (cons (cp 112) (cons (cp 117) (cons (cp 114) (cons (cp 115) (cons (cp 101) nil)))))) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-rbrace (head (definition cetta-rho-k-rbrace (left (char (cp 125)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-rparen (head (definition cetta-rho-k-rparen (left (char (cp 41)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-star (head (definition cetta-rho-k-star (left (char (cp 42)) (ref cetta-rho-skip)))) (body))
    (rule def-cetta-rho-k-zero (head (definition cetta-rho-k-zero (left (char (cp 48)) (ref cetta-rho-skip)))) (body))))
