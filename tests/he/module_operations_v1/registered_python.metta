!(case (register-module! "registered_provider.py") (((Error $a $b) refused) ($ok registered)))
! registered-provider-value
!(import! &self registered_provider)
! registered-provider-value
!(case (register-module! "registered_pypkg") (((Error $a $b) refused) ($ok registered)))
! registered-package-value
!(import! &self registered_pypkg)
! registered-package-value
!(case (register-module! "registered_missing.py") (((Error $a $b) refused) ($ok registered)))
