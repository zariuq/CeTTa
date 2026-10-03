; A library operation whose native gives no answer gives none in PeTTa: the
; library's PeTTa spelling calls the native directly, so no wrapper turns
; the native's missing answer into a value.  Run with one extra argument,
; probe-arg, which the second query answers.
!(import! &self (library system))
!(collapse (system:arg 99))
!(collapse (system:arg 0))
