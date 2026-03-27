!(import! &self system)

!(system-argc)
!(system-has-args)
!(system-arg 0)
!(system-flag-arg? "--log")
!(system-flag-arg? foo)
!(system-getenv-or-default "CETTA_TEST_MISSING" "fallback")
!(get-type (system-cwd))
