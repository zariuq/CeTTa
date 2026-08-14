; Candidate census probe: quoted nominal assertion as declared output.
(: Count (Newtype Number))

(: quoted-count (-[det]-> Count))
(= (quoted-count) (quote (brand Count 1)))
