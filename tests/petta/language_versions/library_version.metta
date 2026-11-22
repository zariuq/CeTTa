; Every PeTTa version reads the same lib_roman.  Its error helpers check by
; their declarations: if-error-number and if-error-bool answer the value when
; it has their type, and their fallback when the value is an error.
!(import! &self (library lib_roman))
!(if-error-number 5 0)
!(if-error-number (Error boom "x") 0)
!(catch (collapse (checked-number "five")))
!(if-error-bool (Error boom "x") True)
