; A finite extensional provider for the structurally open native-type canary.
; Its facts are reflected and decoded through the same generic capability
; boundary used by larger proof-state providers.

(gslt-presentation-v1 OSLFNativeOpenCapabilityV1
  (signature
    (operator canary-open-edge-v1 2)
  )
  (equations)
  (rewrites
    (rule canary-open-capability-left-right-v1
      (head (canary-open-edge-v1 left right))
      (body))
    (rule canary-open-capability-zeta-v1
      (head (canary-open-edge-v1 zeta zeta))
      (body))
  ))
