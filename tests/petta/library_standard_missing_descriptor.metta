!(test
   (size-atom
     (collapse
       (library library-member-that-does-not-exist)))
   1)

; The descriptor is syntactic.  Import is the later partial observer, so an
; absent member contributes no result rather than erasing the descriptor.
!(import! &self (library library-member-that-does-not-exist))
