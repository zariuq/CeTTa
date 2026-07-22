; CeTTa strict-core rho concrete syntax.
; Bare-name scope and quote-reset validity are replay-time relational actions;
; the grammar records the necessary binder and name nodes without embedding
; an environment in the generic parser kernel.
(gslt-presentation-v1 CeTTaRhoReaderSyntaxV1
  (signature)
  (equations)
  (rewrites
    (rule def-cetta-rho-binder
      (head (definition cetta-rho-binder
        (node binder
          (alt
            (right (ref cetta-rho-k-dollar) (ref cetta-rho-ident))
            (ref cetta-rho-ident)))))
      (body))
    (rule def-cetta-rho-variable-name
      (head (definition cetta-rho-variable-name
        (node variable
          (right (ref cetta-rho-k-dollar) (ref cetta-rho-ident)))))
      (body))
    (rule def-cetta-rho-bare-name
      (head (definition cetta-rho-bare-name
        (node bare-name (ref cetta-rho-ident))))
      (body))
    (rule def-cetta-rho-process-leading-bare-name
      (head (definition cetta-rho-process-leading-bare-name
        (node bare-name (ref cetta-rho-process-leading-ident))))
      (body))
    (rule def-cetta-rho-quoted-name
      (head (definition cetta-rho-quoted-name
        (node NQuote
          (right
            (ref cetta-rho-k-at)
            (right
              (ref cetta-rho-k-lbrace)
              (left (ref cetta-rho-process) (ref cetta-rho-k-rbrace)))))))
      (body))
    (rule def-cetta-rho-name
      (head (definition cetta-rho-name
        (alt
          (ref cetta-rho-quoted-name)
          (alt (ref cetta-rho-variable-name) (ref cetta-rho-bare-name)))))
      (body))
    (rule def-cetta-rho-process-leading-name
      (head (definition cetta-rho-process-leading-name
        (alt
          (ref cetta-rho-quoted-name)
          (alt
            (ref cetta-rho-variable-name)
            (ref cetta-rho-process-leading-bare-name)))))
      (body))

    (rule def-cetta-rho-group
      (head (definition cetta-rho-group
        (right
          (ref cetta-rho-k-lbrace)
          (left (ref cetta-rho-process) (ref cetta-rho-k-rbrace)))))
      (body))
    (rule def-cetta-rho-nil
      (head (definition cetta-rho-nil
        (node PZero (ref cetta-rho-k-zero))))
      (body))
    (rule def-cetta-rho-receive
      (head (definition cetta-rho-receive
        (node PInput
          (right
            (ref cetta-rho-k-for)
            (right
              (ref cetta-rho-k-lparen)
              (seq
                (left (ref cetta-rho-binder) (ref cetta-rho-k-larrow))
                (seq
                  (left (ref cetta-rho-name) (ref cetta-rho-k-rparen))
                  (right
                    (ref cetta-rho-k-lbrace)
                    (left
                      (ref cetta-rho-process)
                      (ref cetta-rho-k-rbrace))))))))))
      (body))
    (rule def-cetta-rho-drop
      (head (definition cetta-rho-drop
        (node PDrop
          (right (ref cetta-rho-k-star) (ref cetta-rho-name)))))
      (body))
    (rule def-cetta-rho-send
      (head (definition cetta-rho-send
        (node POutput
          (seq
            (ref cetta-rho-process-leading-name)
            (right
              (ref cetta-rho-k-bang)
              (right
                (ref cetta-rho-k-lparen)
                (left
                  (ref cetta-rho-process)
                  (ref cetta-rho-k-rparen))))))))
      (body))
    (rule def-cetta-rho-process-atom
      (head (definition cetta-rho-process-atom
        (alt
          (ref cetta-rho-group)
          (alt
            (ref cetta-rho-nil)
            (alt
              (ref cetta-rho-receive)
              (alt (ref cetta-rho-drop) (ref cetta-rho-send)))))))
      (body))
    (rule def-cetta-rho-process
      (head (definition cetta-rho-process
        (node PPar
          (seq
            (ref cetta-rho-process-atom)
            (star
              (right
                (ref cetta-rho-k-par)
                (ref cetta-rho-process-atom)))))))
      (body))
    (rule def-cetta-rho-document
      (head (definition cetta-rho-document
        (node document
          (right
            (ref cetta-rho-skip)
            (left (ref cetta-rho-process) eof)))))
      (body))))
