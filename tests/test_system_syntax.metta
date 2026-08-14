!(import! &self system)

!(system:argc)
!(system:has-args)
!(system:arg 0)
!(system:is-flag "--log")
!(system:is-flag foo)
!(system:getenv-or-default "CETTA_TEST_MISSING" "fallback")
!(get-type (system:cwd))
!(assertEqualToResult
  (let $t0 (system:monotonic-ns)
    (let $t1 (system:monotonic-ns)
      (and (== (get-type $t0) Number) (<= $t0 $t1))))
  (True))
