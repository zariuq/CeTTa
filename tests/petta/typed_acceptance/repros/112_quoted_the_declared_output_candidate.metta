; Candidate census probe: quoted explicit type assertion as declared output.
(: quoted-number (-[det]-> Number))
(= (quoted-number) (quote (the Number 1)))
