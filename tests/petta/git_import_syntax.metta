; The package operation is inert until its native compatibility library is
; imported.  Once enabled, arity and argument-type failures are explicit.
!(git-import! "not-run")
!(import! &self (library lib_import))
!(git-import! "not-run" "" "./repos" "short")
