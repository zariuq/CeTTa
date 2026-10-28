; The library a version reads: PeTTa 1.1 reads its main line's lib_roman,
; corrected stable PeTTa retains its stable library, without if-error-number.
!(import! &self (library lib_roman))
!(if-error-number 5 0)
