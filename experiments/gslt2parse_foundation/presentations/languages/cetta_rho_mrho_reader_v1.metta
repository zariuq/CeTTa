; CeTTa strict-core .mrho notation over shared HE scalar classes.
; Its semantic actions name the same rhoCalc constructors as the .rho
; presentation; binder identity and quote-reset are replay-time relations.
(gslt-presentation-v1 CeTTaRhoMRhoReaderSyntaxV1
  (signature)
  (equations)
  (rewrites
    (rule def-cetta-rho-mrho-k-lparen
      (head (definition cetta-rho-mrho-k-lparen
        (left (char (cp 40)) (ref cetta-rho-mrho-skip))))
      (body))
    (rule def-cetta-rho-mrho-k-rparen
      (head (definition cetta-rho-mrho-k-rparen
        (left (char (cp 41)) (ref cetta-rho-mrho-skip))))
      (body))
    (rule def-cetta-rho-mrho-k-nil
      (head (definition cetta-rho-mrho-k-nil
        (left
          (lit (cons (cp 114) (cons (cp 104) (cons (cp 111) (cons (cp 58) (cons (cp 110) (cons (cp 105) (cons (cp 108) nil))))))))
          (ref cetta-rho-mrho-skip))))
      (body))
    (rule def-cetta-rho-mrho-k-par
      (head (definition cetta-rho-mrho-k-par
        (left
          (lit (cons (cp 114) (cons (cp 104) (cons (cp 111) (cons (cp 58) (cons (cp 112) (cons (cp 97) (cons (cp 114) nil))))))))
          (ref cetta-rho-mrho-skip))))
      (body))
    (rule def-cetta-rho-mrho-k-send
      (head (definition cetta-rho-mrho-k-send
        (left
          (lit (cons (cp 114) (cons (cp 104) (cons (cp 111) (cons (cp 58) (cons (cp 115) (cons (cp 101) (cons (cp 110) (cons (cp 100) nil)))))))))
          (ref cetta-rho-mrho-skip))))
      (body))
    (rule def-cetta-rho-mrho-k-recv
      (head (definition cetta-rho-mrho-k-recv
        (left
          (lit (cons (cp 114) (cons (cp 104) (cons (cp 111) (cons (cp 58) (cons (cp 114) (cons (cp 101) (cons (cp 99) (cons (cp 118) nil)))))))))
          (ref cetta-rho-mrho-skip))))
      (body))
    (rule def-cetta-rho-mrho-k-quote
      (head (definition cetta-rho-mrho-k-quote
        (left
          (lit (cons (cp 114) (cons (cp 104) (cons (cp 111) (cons (cp 58) (cons (cp 113) (cons (cp 117) (cons (cp 111) (cons (cp 116) (cons (cp 101) nil))))))))))
          (ref cetta-rho-mrho-skip))))
      (body))
    (rule def-cetta-rho-mrho-k-drop
      (head (definition cetta-rho-mrho-k-drop
        (left
          (lit (cons (cp 114) (cons (cp 104) (cons (cp 111) (cons (cp 58) (cons (cp 100) (cons (cp 114) (cons (cp 111) (cons (cp 112) nil)))))))))
          (ref cetta-rho-mrho-skip))))
      (body))

    (rule def-cetta-rho-mrho-comment
      (head (definition cetta-rho-mrho-comment
        (node comment
          (right
            (char (cp 59))
            (left
              (star (class he-non-newline-scalar))
              (alt (class he-newline-scalar) eof))))))
      (body))
    (rule def-cetta-rho-mrho-layout-unit
      (head (definition cetta-rho-mrho-layout-unit
        (alt
          (class he-whitespace-scalar)
          (ref cetta-rho-mrho-comment))))
      (body))
    (rule def-cetta-rho-mrho-skip
      (head (definition cetta-rho-mrho-skip
        (star (ref cetta-rho-mrho-layout-unit))))
      (body))

    (rule def-cetta-rho-mrho-variable
      (head (definition cetta-rho-mrho-variable
        (node variable
          (left
            (right
              (char (cp 36))
              (star (class he-variable-tail-scalar)))
            (ref cetta-rho-mrho-skip)))))
      (body))
    (rule def-cetta-rho-mrho-binder
      (head (definition cetta-rho-mrho-binder
        (node binder (ref cetta-rho-mrho-variable))))
      (body))
    (rule def-cetta-rho-mrho-quote
      (head (definition cetta-rho-mrho-quote
        (node NQuote
          (right
            (ref cetta-rho-mrho-k-lparen)
            (right
              (ref cetta-rho-mrho-k-quote)
              (left
                (ref cetta-rho-mrho-process)
                (ref cetta-rho-mrho-k-rparen)))))))
      (body))
    (rule def-cetta-rho-mrho-name
      (head (definition cetta-rho-mrho-name
        (alt
          (ref cetta-rho-mrho-quote)
          (ref cetta-rho-mrho-variable))))
      (body))

    (rule def-cetta-rho-mrho-nil
      (head (definition cetta-rho-mrho-nil
        (node PZero (ref cetta-rho-mrho-k-nil))))
      (body))
    (rule def-cetta-rho-mrho-drop
      (head (definition cetta-rho-mrho-drop
        (node PDrop
          (right
            (ref cetta-rho-mrho-k-lparen)
            (right
              (ref cetta-rho-mrho-k-drop)
              (left
                (ref cetta-rho-mrho-name)
                (ref cetta-rho-mrho-k-rparen)))))))
      (body))
    (rule def-cetta-rho-mrho-send
      (head (definition cetta-rho-mrho-send
        (node POutput
          (right
            (ref cetta-rho-mrho-k-lparen)
            (right
              (ref cetta-rho-mrho-k-send)
              (seq
                (ref cetta-rho-mrho-name)
                (left
                  (ref cetta-rho-mrho-process)
                  (ref cetta-rho-mrho-k-rparen))))))))
      (body))
    (rule def-cetta-rho-mrho-receive
      (head (definition cetta-rho-mrho-receive
        (node PInput
          (right
            (ref cetta-rho-mrho-k-lparen)
            (right
              (ref cetta-rho-mrho-k-recv)
              (seq
                (ref cetta-rho-mrho-name)
                (seq
                  (ref cetta-rho-mrho-binder)
                  (left
                    (ref cetta-rho-mrho-process)
                    (ref cetta-rho-mrho-k-rparen)))))))))
      (body))
    (rule def-cetta-rho-mrho-parallel
      (head (definition cetta-rho-mrho-parallel
        (node PPar
          (right
            (ref cetta-rho-mrho-k-lparen)
            (right
              (ref cetta-rho-mrho-k-par)
              (left
                (star (ref cetta-rho-mrho-process))
                (ref cetta-rho-mrho-k-rparen)))))))
      (body))
    (rule def-cetta-rho-mrho-process
      (head (definition cetta-rho-mrho-process
        (alt
          (ref cetta-rho-mrho-nil)
          (alt
            (ref cetta-rho-mrho-drop)
            (alt
              (ref cetta-rho-mrho-send)
              (alt
                (ref cetta-rho-mrho-receive)
                (ref cetta-rho-mrho-parallel)))))))
      (body))
    (rule def-cetta-rho-mrho-document
      (head (definition cetta-rho-mrho-document
        (node document
          (right
            (ref cetta-rho-mrho-skip)
            (left (ref cetta-rho-mrho-process) eof)))))
      (body))))
