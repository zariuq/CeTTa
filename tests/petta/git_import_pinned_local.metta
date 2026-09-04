; Manual integration fixture.  argv 1..3 are the repository URL or local
; path, destination base, and exact full commit supplied by the harness.
!(import! &self (library lib_import))
!(git-import! (argv 1) "" (argv 2) (argv 3))
!(git-import! (argv 1) "" (argv 2) (argv 3))
!(size-atom (collapse (library metta-attention lib)))
