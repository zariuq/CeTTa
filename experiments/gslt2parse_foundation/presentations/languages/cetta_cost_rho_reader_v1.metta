; CeTTa cost-profile rho concrete syntax. It reuses the strict syntax's
; scanner and identifiers, while its term, process, signature, and stack
; categories remain distinct.
(gslt-presentation-v1 CeTTaCostRhoReaderSyntaxV1
  (signature)
  (equations)
  (rewrites
    (rule def-cetta-cost-rho-signature-atom
      (head (definition cetta-cost-rho-signature-atom
        (node signature (ref cetta-rho-ident))))
      (body))
    (rule def-cetta-cost-rho-signature
      (head (definition cetta-cost-rho-signature
        (node rho:cost:sig-mul
          (seq
            (ref cetta-cost-rho-signature-atom)
            (star
              (right
                (ref cetta-rho-k-star)
                (ref cetta-cost-rho-signature-atom)))))))
      (body))
    (rule def-cetta-cost-rho-leading-signature
      (head (definition cetta-cost-rho-leading-signature
        (node rho:cost:sig-mul
          (seq
            (node signature (ref cetta-rho-process-leading-ident))
            (star
              (right
                (ref cetta-rho-k-star)
                (ref cetta-cost-rho-signature-atom)))))))
      (body))
    (rule def-cetta-cost-rho-binder
      (head (definition cetta-cost-rho-binder
        (node binder
          (alt
            (right (ref cetta-rho-k-dollar) (ref cetta-rho-ident))
            (ref cetta-rho-ident)))))
      (body))
    (rule def-cetta-cost-rho-variable-name
      (head (definition cetta-cost-rho-variable-name
        (node variable
          (right (ref cetta-rho-k-dollar) (ref cetta-rho-ident)))))
      (body))
    (rule def-cetta-cost-rho-quoted-name
      (head (definition cetta-cost-rho-quoted-name
        (node rho:quote
          (right
            (ref cetta-rho-k-at)
            (right
              (ref cetta-rho-k-lbrace)
              (left (ref cetta-cost-rho-term) (ref cetta-rho-k-rbrace)))))))
      (body))
    (rule def-cetta-cost-rho-name
      (head (definition cetta-cost-rho-name
        (alt
          (ref cetta-cost-rho-quoted-name)
          (alt
            (ref cetta-cost-rho-variable-name)
            (ref cetta-cost-rho-signature)))))
      (body))
    (rule def-cetta-cost-rho-process-leading-name
      (head (definition cetta-cost-rho-process-leading-name
        (alt
          (ref cetta-cost-rho-quoted-name)
          (alt
            (ref cetta-cost-rho-variable-name)
            (ref cetta-cost-rho-leading-signature)))))
      (body))

    (rule def-cetta-cost-rho-process-group
      (head (definition cetta-cost-rho-process-group
        (right
          (ref cetta-rho-k-lbrace)
          (left (ref cetta-cost-rho-process) (ref cetta-rho-k-rbrace)))))
      (body))
    (rule def-cetta-cost-rho-process-nil
      (head (definition cetta-cost-rho-process-nil
        (node rho:nil (ref cetta-rho-k-zero))))
      (body))
    (rule def-cetta-cost-rho-receive
      (head (definition cetta-cost-rho-receive
        (node rho:recv
          (right
            (ref cetta-rho-k-for)
            (right
              (ref cetta-rho-k-lparen)
              (seq
                (left (ref cetta-cost-rho-binder) (ref cetta-rho-k-larrow))
                (seq
                  (left (ref cetta-cost-rho-name) (ref cetta-rho-k-rparen))
                  (right
                    (ref cetta-rho-k-lbrace)
                    (left
                      (ref cetta-cost-rho-term)
                      (ref cetta-rho-k-rbrace))))))))))
      (body))
    (rule def-cetta-cost-rho-send
      (head (definition cetta-cost-rho-send
        (node rho:send
          (seq
            (ref cetta-cost-rho-process-leading-name)
            (right
              (ref cetta-rho-k-bang)
              (right
                (ref cetta-rho-k-lparen)
                (left
                  (ref cetta-cost-rho-term)
                  (ref cetta-rho-k-rparen))))))))
      (body))
    (rule def-cetta-cost-rho-process-atom
      (head (definition cetta-cost-rho-process-atom
        (alt
          (ref cetta-cost-rho-process-group)
          (alt
            (ref cetta-cost-rho-process-nil)
            (alt (ref cetta-cost-rho-receive) (ref cetta-cost-rho-send))))))
      (body))
    (rule def-cetta-cost-rho-process
      (head (definition cetta-cost-rho-process
        (node rho:par
          (seq
            (ref cetta-cost-rho-process-atom)
            (star
              (right
                (ref cetta-rho-k-par)
                (ref cetta-cost-rho-process-atom)))))))
      (body))

    (rule def-cetta-cost-rho-stack
      (head (definition cetta-cost-rho-stack
        (alt
          (node rho:cost:stack-empty (ref cetta-rho-k-empty))
          (node rho:cost:stack-cons
            (seq
              (left
                (ref cetta-cost-rho-signature)
                (ref cetta-rho-k-colon))
              (ref cetta-cost-rho-stack))))))
      (body))
    (rule def-cetta-cost-rho-nil
      (head (definition cetta-cost-rho-nil
        (node rho:cost:nil (ref cetta-rho-k-empty))))
      (body))
    (rule def-cetta-cost-rho-purse
      (head (definition cetta-cost-rho-purse
        (node rho:cost:purse
          (right
            (ref cetta-rho-k-purse)
            (seq
              (ref cetta-cost-rho-name)
              (right
                (ref cetta-rho-k-lbrace)
                (left
                  (ref cetta-cost-rho-stack)
                  (ref cetta-rho-k-rbrace))))))))
      (body))
    (rule def-cetta-cost-rho-drop
      (head (definition cetta-cost-rho-drop
        (node rho:drop
          (right (ref cetta-rho-k-star) (ref cetta-cost-rho-name)))))
      (body))
    (rule def-cetta-cost-rho-signed
      (head (definition cetta-cost-rho-signed
        (node rho:cost:signed
          (seq
            (right
              (ref cetta-rho-k-lbrace)
              (left
                (ref cetta-cost-rho-process)
                (ref cetta-rho-k-rbrace)))
            (ref cetta-cost-rho-signature)))))
      (body))
    (rule def-cetta-cost-rho-term-atom
      (head (definition cetta-cost-rho-term-atom
        (alt
          (ref cetta-cost-rho-nil)
          (alt
            (ref cetta-cost-rho-purse)
            (alt
              (ref cetta-cost-rho-drop)
              (ref cetta-cost-rho-signed))))))
      (body))
    (rule def-cetta-cost-rho-term
      (head (definition cetta-cost-rho-term
        (node rho:cost:par
          (seq
            (ref cetta-cost-rho-term-atom)
            (star
              (right
                (ref cetta-rho-k-par)
                (ref cetta-cost-rho-term-atom)))))))
      (body))
    (rule def-cetta-cost-rho-document
      (head (definition cetta-cost-rho-document
        (node document
          (right
            (ref cetta-rho-skip)
            (left (ref cetta-cost-rho-term) eof)))))
      (body))))
