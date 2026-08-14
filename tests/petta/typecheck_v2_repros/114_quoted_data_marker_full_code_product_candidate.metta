; Candidate census probe: quote preserves the data marker as held code.
(: quoted-data-code (-[det]-> (Atom Atom Number)))
(= (quoted-data-code) (quote (data tag 1)))
