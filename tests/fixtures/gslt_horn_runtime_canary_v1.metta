(gslt-presentation-v1 GsltHornRuntimeCanaryV1
  (signature
    (operator canary-link 2)
    (operator canary-route 2))
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
  ))
