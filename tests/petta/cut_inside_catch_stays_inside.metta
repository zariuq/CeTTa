; catch calls its body as Prolog's catch/3 calls its goal: a cut inside
; commits the body's own choices, never the clause that wrote the catch,
; as once, collapse, with_mutex and transaction already keep it.
(= (f) (once (progn (superpose (1 2)) (cut) inner)))
(= (f) second)
!(collapse (f))
(= (g) (let $l (collapse (progn (superpose (1 2)) (cut) x)) (got $l)))
(= (g) second)
!(collapse (g))
(= (h) (with_mutex m (progn (superpose (1 2)) (cut) inner)))
(= (h) second)
!(collapse (h))
(= (t) (transaction (progn (superpose (1 2)) (cut) inner)))
(= (t) second)
!(collapse (t))
(= (c) (catch (progn (superpose (1 2)) (cut) inner)))
(= (c) second)
!(collapse (c))
; The cut commits the body's choices: one answer, not three.
!(collapse (catch (progn (superpose (1 2 3)) (cut) inner)))
; Choices made before the catch stay: each meets the catch once.
(= (m) (progn (superpose (1 2)) (catch (progn (superpose (a b)) (cut) inner))))
(= (m) second)
!(collapse (m))
; A cut outside any scope still commits its clause.
(= (d) (progn (superpose (1 2)) (cut) inner))
(= (d) second)
!(collapse (d))
; The catch still turns an error raised after the cut into a value.
(= (e) (catch (progn (superpose (1 2)) (cut) (size-atom q q))))
(= (e) second)
!(collapse (e))
