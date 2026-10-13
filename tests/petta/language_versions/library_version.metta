; The library a version reads: PeTTa 1.1 reads its main line's lib_roman,
; PeTTa 1.0.6 the one of release v1.0.6, which has no if-error-number.
!(import! &self (library lib_roman))
!(if-error-number 5 0)
