(set:profile hol)
!(bind! &s (let $sp (new-space) (let $_ (add-atom $sp (set:profile hol)) $sp)))
; closed instance
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelConst 2) (LevelConst 0))) (Pi (Sort (LevelConst 2)) (Sort (LevelConst 0))))))
; caller parameter outside schema indexes
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelParam 2) (LevelConst 0))) (Pi (Sort (LevelParam 2)) (Sort (LevelConst 0))))))
; identity instance
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelParam 0) (LevelParam 1))) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))))))
; caller parameter at a schema index
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelParam 1) (LevelConst 0))) (Pi (Sort (LevelParam 1)) (Sort (LevelConst 0))))))
; captured caller parameter refused
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelParam 1) (LevelConst 0))) (Pi (Sort (LevelConst 0)) (Sort (LevelConst 0))))))
; parameter permutation
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelParam 1) (LevelParam 0))) (Pi (Sort (LevelParam 1)) (Sort (LevelParam 0))))))
; wrong permutation refused
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelParam 1) (LevelParam 0))) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))))))
; successor of a caller parameter
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelSucc (LevelParam 1)) (LevelConst 0))) (Pi (Sort (LevelSucc (LevelParam 1))) (Sort (LevelConst 0))))))
; captured successor refused
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelSucc (LevelParam 1)) (LevelConst 0))) (Pi (Sort (LevelConst 1)) (Sort (LevelConst 0))))))
; caller parameter at its own schema index
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelSucc (LevelParam 0)) (LevelConst 0))) (Pi (Sort (LevelSucc (LevelParam 0))) (Sort (LevelConst 0))))))
; shared caller parameter
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly (LevelParam 1) (LevelParam 1))) (Pi (Sort (LevelParam 1)) (Sort (LevelParam 1))))))
; omitted arguments select the lowest instance
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly)) (Pi (Sort (LevelConst 0)) (Sort (LevelConst 0))))))
; lowest domain mismatch refused
!(car-atom (try (type:check &s (PrimeScoped (PrimeCtxDecl (DeclConst poly (LevelParam 0) (LevelParam 1)) (Pi (Sort (LevelParam 0)) (Sort (LevelParam 1))) PrimeCtxNil) (DeclConst poly)) (Pi (Sort (LevelConst 1)) (Sort (LevelConst 0))))))
