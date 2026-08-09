(gslt-presentation-v1 RenamedCompiledCanaryV1
  (signature
    (operator q-str 1)
    (operator q-sym 1)
    (operator canary-evaluate 2))
  (equations)
  (rewrites
    (rule renamed-answer
      (head
        (canary-evaluate
          ?program
          (q-sym (q-str "renamed-answer"))))
      (body))
  ))
