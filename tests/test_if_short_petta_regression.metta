; Two-argument if should behave like the upstream Petta surface:
; on failure it should simply miss.

!(test (if True 42)
       42)

!(test (collapse (if False 42))
       ())
