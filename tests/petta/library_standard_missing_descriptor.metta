!(test
   (size-atom
     (collapse
       (library library-member-that-does-not-exist)))
   1)

; The descriptor is syntactic: an absent member still has its one
; descriptor.  Importing it raises the missing source's catchable existence
; error, as the reference does (P-LIB-2).
!(test
   (car-atom (catch (import! &self (library library-member-that-does-not-exist))))
   Error)
