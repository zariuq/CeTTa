; Candidate census probe: a data marker held by quote is still code.
(: quoted-data (-[det]-> (Atom Number)))
(= (quoted-data) (quote (data tag 1)))
