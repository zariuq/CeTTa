; Import isolation: a native module's words are evaluated only after that module is imported.
; Importing a library, or another module, enables none of them, and importing a native module
; never makes a library look imported (each import has its own bit in the session's mask).

; Before any import, the modules' words are symbols.
!(__cetta_lib_mgm_call (nat.eq (nat.z) (nat.z)))
!(__cetta_lib_holm_check "/nonexistent.holr" standard)
!(heap:empty)

; A library whose bit the compiled modules once shared enables neither of them.
!(import! &self sat)
!(__cetta_lib_mgm_call (nat.eq (nat.z) (nat.z)))
!(__cetta_lib_holm_check "/nonexistent.holr" standard)

; A native module imported first neither enables a library's natives nor stops its import.
!(import! &self heap)
!(__cetta_lib_asp_solve ())
!(import! &self asp)
!(asp:solve ())

; One compiled module does not enable another; each answers once imported.
!(import! &self holmodule)
!(__cetta_lib_mgm_call (nat.eq (nat.z) (nat.z)))
!(import! &self mgmodule)
!(mgm:call (nat.eq (nat.z) (nat.z)))
