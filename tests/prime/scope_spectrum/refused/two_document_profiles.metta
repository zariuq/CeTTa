; Two whole-document profile declarations that disagree: the document is
; refused, never read under one of them silently.
(scope:profile explicit-capture)
(scope:profile query-wide)
!(+ 1 2)
