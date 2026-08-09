(gslt-presentation-v1 RenamedPipelineCanaryV1
  (signature
    (operator canary-classify 3)
    (operator canary-observe 4)
    (operator canary-observed 1)
    (operator canary-produce 4)
    (operator canary-produced-cons 2)
    (operator canary-produced-member 2)
    (operator canary-request 1)
    (operator gslt-produced 2)
    (operator q-app 2)
    (operator q-str 1)
    (operator q-sym 1))
  (equations)
  (rewrites
    (rule canary-classifies-renamed-command
      (head
        (canary-classify
          ?occurrence
          (q-app (q-sym (q-str "canary-run")) q-nil)
          (canary-request ?occurrence)))
      (body))
    (rule canary-produces-renamed-answer
      (head
        (canary-produce
          ?program (canary-request ?occurrence)
          canary-evidence
          (q-sym (q-str "renamed-pipeline-answer"))))
      (body))
    (rule canary-produced-member-here
      (head
        (canary-produced-member
          (canary-produced-cons ?item ?rest) ?item))
      (body))
    (rule canary-produced-member-rest
      (head
        (canary-produced-member
          (canary-produced-cons ?head ?rest) ?item))
      (body (canary-produced-member ?rest ?item)))
    (rule canary-observes-renamed-answer
      (head
        (canary-observe
          (canary-request ?occurrence) ?bag
          (canary-observed ?evidence) ?result))
      (body
        (canary-produced-member
          ?bag (gslt-produced ?evidence ?result))))
  ))
