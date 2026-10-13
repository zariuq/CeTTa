(hol:profile core)

; Native arithmetic for the operations of arith.binary.metta, inserted after
; its definitions by tests/support/check_prime_arith_oracle.py.  Each reading
; says how a declared datatype writes numbers; each oracle names an admitted
; operation, its equations as its specification, and the backend that
; computes it.  Admission compares the backend with the equations on small
; calls; every answer computed natively is followed by its trust record.
!(hol:numerals &self pos (positive one bit0 bit1))
!(hol:numerals &self nat (natural n0 npos))
!(hol:numerals &self int (integer z0 zpos zneg))
!(hol:numerals &self rat (fraction over))
!(hol:numerals &self ord (ordering less same more))
!(hol:numerals &self divided (quotient-remainder quot-rem))
!(hol:oracle &self padd (equations padd) (backend gmp-mpz add))
!(hol:oracle &self pmul (equations pmul) (backend gmp-mpz mul))
!(hol:oracle &self pcmp (equations pcmp) (backend gmp-mpz compare))
!(hol:oracle &self ncmp (equations ncmp) (backend gmp-mpz compare))
!(hol:oracle &self nsub (equations nsub) (backend gmp-mpz sub-truncated))
!(hol:oracle &self ndivmod (equations ndivmod) (backend gmp-mpz divmod))
!(hol:oracle &self pgcd (equations pgcd) (backend gmp-mpz gcd))
!(hol:oracle &self qmake (equations qmake) (backend gmp-mpz normalize))
!(hol:oracle &self qadd (equations qadd) (backend gmp-mpz add))
!(hol:oracle &self qsub (equations qsub) (backend gmp-mpz sub))
!(hol:oracle &self qmul (equations qmul) (backend gmp-mpz mul))
!(hol:oracle &self qcmp (equations qcmp) (backend gmp-mpz compare))
