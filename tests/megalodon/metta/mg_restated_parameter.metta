; Megalodon documents restate their predecessors by hash.  A parameter
; declared by the hash of a known definition of its type is that
; definition; the same hash at another type is still a conflict.
!(import! &self megalodon)
!(let $state (mg:new-theory-state egal) (bind! &egal $state))
(= (AUTH) (mg:authority (get-state &egal)))
!(mg:import (AUTH) "tests/megalodon/fixtures/pfghotg_churchnum.sexpr" (axioms all))
!(mg:import (AUTH) "tests/megalodon/fixtures/pfghotg_restated_eq.sexpr" (axioms all))
!(mg:try-import (AUTH) "tests/megalodon/fixtures/pfghotg_restated_eq_other_type.sexpr" (axioms all))
