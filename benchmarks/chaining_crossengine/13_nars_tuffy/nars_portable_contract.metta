; Small positive and negative contracts for the portable NARS core.
!(import! &self ./nars_tuffy_kb)

!(assertEqual (StampConcat (3 1) (2 1)) (1 1 2 3))
!(assertEqual (list:unique (3 1 3 2 1)) (3 1 2))
!(assertEqual (list:remove-all 2 (1 2 2 3)) (1 3))
!(assertEqual
  (PriorityRank
    (Sentence (derived (stv 0.5 (min 0.99 (max 0.2 0.7)))) (1 2)))
  0.7)
!(assertEqual (NARS.UnaryDerivations (--> a b) (1)) ())
!(assertEqual
  (let (Sentence $x $ev) (list:at (kb) 0)
    (NARS.FusedDerivations $x $ev (kb)))
  (let (Sentence $x $ev) (list:at (kb) 0)
    (list:append
      (NARS.BinaryDerivations $x $ev (kb))
      (NARS.UnaryDerivations $x $ev))))
!(assertEqual
  (NARS.Query (kb) (--> Edward ([] smokes)) 0 10 100)
  ((stv 1.0 0.9) (10)))
!(assertEqual
  (NARS.Query (kb) (--> Edward ([] cancerous)) 0 10 100)
  ())

(= (mini-kb-disjoint)
   ((Sentence ((--> a b) (stv 1.0 1.0)) (1))
    (Sentence ((--> b c) (stv 1.0 1.0)) (2))))

(= (mini-kb-overlap)
   ((Sentence ((--> a b) (stv 1.0 1.0)) (1))
    (Sentence ((--> b c) (stv 1.0 1.0)) (1))))

; One selected task may derive the target when evidence stamps are disjoint.
!(assertEqual
  (NARS.Query (mini-kb-disjoint) (--> a c) 1 10 10)
  ((stv 1.0 1.0) (1 2)))

; The identical-stamp negative refuses to count one observation twice.
!(assertEqual
  (NARS.Query (mini-kb-overlap) (--> a c) 1 10 10)
  ())
