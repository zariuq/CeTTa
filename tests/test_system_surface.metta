!(import! &self system)

!(system:argc)
!(system:has-args)
!(system:arg 0)
!(system:is-flag "--log")
!(system:is-flag foo)
!(system:getenv-or-default "CETTA_TEST_MISSING" "fallback")
!(get-type (system:cwd))
