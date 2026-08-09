(gslt-presentation-v1 GsltHornRuntimeCanaryV1
  (signature
    (operator canary-link 2)
    (operator canary-route 2)
    (operator canary-left 1)
    (operator canary-right 1)
    (operator canary-pair 2))
  (equations)
  (rewrites
    (rule canary-link-alpha-beta-one
      (head (canary-link alpha beta))
      (body))
    (rule canary-link-alpha-beta-two
      (head (canary-link alpha beta))
      (body))
    (rule canary-link-beta-gamma
      (head (canary-link beta gamma))
      (body))
    (rule canary-route-two-links
      (head (canary-route ?from ?to))
      (body
        (canary-link ?from ?middle)
        (canary-link ?middle ?to)))
    (rule canary-left-from-link
      (head (canary-left ?shared))
      (body
        (canary-link ?shared beta)))
    (rule canary-right-from-link
      (head (canary-right ?shared))
      (body
        (canary-link beta ?shared)))
    (rule canary-pair-independent-scopes
      (head (canary-pair ?left ?right))
      (body
        (canary-left ?left)
        (canary-right ?right)))
  ))
