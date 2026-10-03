; Native arithmetic for the operations of arith.binary.metta, inserted after
; its definitions by tests/support/check_prime_arith_oracle.py.  Each reading
; says how a declared datatype writes numbers; each oracle names an admitted
; operation, its equations as its specification, and the backend that
; computes it.  Admission compares the backend with the equations on small
; calls; every answer computed natively is followed by its trust record.
!(set:numerals &self pos (positive one bit0 bit1))
!(set:numerals &self nat (natural n0 npos))
!(set:numerals &self int (integer z0 zpos zneg))
!(set:numerals &self rat (fraction over))
!(set:numerals &self ord (ordering less same more))
!(set:numerals &self divided (quotient-remainder quot-rem))
!(set:oracle &self padd (equations padd) (backend gmp-mpz add))
!(set:oracle &self pmul (equations pmul) (backend gmp-mpz mul))
!(set:oracle &self pcmp (equations pcmp) (backend gmp-mpz compare))
!(set:oracle &self ncmp (equations ncmp) (backend gmp-mpz compare))
!(set:oracle &self nsub (equations nsub) (backend gmp-mpz sub-truncated))
!(set:oracle &self ndivmod (equations ndivmod) (backend gmp-mpz divmod))
!(set:oracle &self pgcd (equations pgcd) (backend gmp-mpz gcd))
!(set:oracle &self qmake (equations qmake) (backend gmp-mpz normalize))
!(set:oracle &self qadd (equations qadd) (backend gmp-mpz add))
!(set:oracle &self qsub (equations qsub) (backend gmp-mpz sub))
!(set:oracle &self qmul (equations qmul) (backend gmp-mpz mul))
!(set:oracle &self qcmp (equations qcmp) (backend gmp-mpz compare))
