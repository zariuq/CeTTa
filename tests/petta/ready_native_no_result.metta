; A library operation that gives no answer to ready arguments fails: its
; no-result marker never becomes a value.
!(import! &self (library system))
!(let $i 99 (collapse (__cetta_lib_system_arg $i)))
