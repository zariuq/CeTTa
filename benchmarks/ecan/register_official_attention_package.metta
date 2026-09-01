; Host-independent package registration for the official ECAN source tree.
; The package's own SWI test harness also registers a local library_path/1;
; that foreign-host detail is harmless once the PeTTa package root exists.

!(import! &self (library lib_import))
!(git-import! "https://github.com/iCog-Labs-Dev/metta-attention.git")
