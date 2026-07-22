(gslt-presentation-v1 PositiveGuardCanaryV1
  (signature)
  (equations)
  (rewrites
    (rule define-guard-root
      (head
        (definition guard-root
          (peek
            (alt
              (eps left-value)
              (char (cp 955))))))
      (body))
    (rule define-guard-nested
      (head
        (definition guard-nested
          (node guard-pair
            (seq
              (peek (char (cp 97)))
              (opt (peek eof))))))
      (body))
    (rule define-guard-absent
      (head
        (definition guard-absent
          (seq (char (cp 97)) (char (cp 98)))))
      (body))
  ))
