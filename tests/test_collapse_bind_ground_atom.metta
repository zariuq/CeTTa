; A ground answer has an empty saved environment, observed by printing.
!(collapse-bind 1)

; The saved environment is grounded, not traversable syntax.
!(assertEqualToResult
  (chain (collapse-bind 1) $saved
    (unify $saved ((1 $env)) (get-metatype $env) Empty))
  (Grounded))
