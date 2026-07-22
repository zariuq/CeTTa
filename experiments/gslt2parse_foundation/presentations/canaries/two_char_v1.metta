; A language presentation containing only data. It accepts exactly "ab" at
; the declared root when composed with SyntaxCoreV1.

(gslt-presentation-v1 TwoCharV1
  (signature)
  (equations)
  (rewrites
    (rule define-two-char-root
      (head
        (definition
          two-char-root
          (node two-char (seq (char (cp 97)) (char (cp 98))))))
      (body))))
