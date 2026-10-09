; HE reads one form at a time. A run that returns at an Error value never
; reads the text after it, so the unclosed form below is not reported.
!(+ 1 2)
!(import! &self no_such_module_before_fault)
!(+ 3 4)
(unclosed
