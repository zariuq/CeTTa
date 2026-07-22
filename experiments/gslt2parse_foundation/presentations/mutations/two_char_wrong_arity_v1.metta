; Invalid canary: seq is declared with arity two, not one.

(gslt-presentation-v1 TwoCharWrongArityV1
  (signature)
  (equations)
  (rewrites
    (rule define-two-char-root-wrong-arity
      (head
        (definition
          two-char-root
          (node two-char (seq (char (cp 97))))))
      (body))))
