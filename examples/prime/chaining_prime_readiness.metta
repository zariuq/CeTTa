; Source-faithful curried chaining is the compatibility floor.  This fixture
; exercises the stronger Prime realization: programs are ordinary inhabitants
; of the indexed family `hyp`, proof occurrences are retained, and typed path
; synthesis excludes incompatible compositions by their family indices.

!(import! &self ../../lib/ilp/prime_native_mil.metta)

(: chain:sort (u 0))
(: chain:alpha chain:sort)
(: chain:beta chain:sort)
(: chain:gamma chain:sort)
(: chain:delta chain:sort)
(: chain:primitive
   (-> chain:sort (-> chain:sort (u 1))))

(: chain:encode-symbol
   (chain:primitive chain:alpha chain:beta))
(: chain:step-symbol
   (chain:primitive chain:beta chain:gamma))
(: chain:finish-symbol
   (chain:primitive chain:gamma chain:delta))
(: chain:loop-symbol
   (chain:primitive chain:beta chain:beta))

(: chain:encode (-> u0 (-> u0 u1)))
(: chain:step (-> u0 (-> u0 u1)))
(: chain:finish (-> u0 (-> u0 u1)))
(: chain:loop (-> u0 (-> u0 u1)))

(: chain:a u0)
(: chain:b u0)
(: chain:c u0)
(: chain:d u0)
(: chain:encode-a-b-left
   (chain:encode chain:a chain:b))
(: chain:encode-a-b-right
   (chain:encode chain:a chain:b))
(: chain:step-b-c
   (chain:step chain:b chain:c))
(: chain:finish-c-d
   (chain:finish chain:c chain:d))
(: chain:loop-b-b
   (chain:loop chain:b chain:b))

(= (hyp:carrier chain:alpha) u0)
(= (hyp:carrier chain:beta) u0)
(= (hyp:carrier chain:gamma) u0)
(= (hyp:carrier chain:delta) u0)
(= (hyp:meaning chain:alpha chain:beta
     chain:encode-symbol)
   chain:encode)
(= (hyp:meaning chain:beta chain:gamma
     chain:step-symbol)
   chain:step)
(= (hyp:meaning chain:gamma chain:delta
     chain:finish-symbol)
   chain:finish)
(= (hyp:meaning chain:beta chain:beta
     chain:loop-symbol)
   chain:loop)

; Two authored evidence occurrences deliberately share their endpoints.
(= (chain:encode chain:a)
   (rel:edge chain:b chain:encode-a-b-left))
(= (chain:encode chain:a)
   (rel:edge chain:b chain:encode-a-b-right))
(= (chain:step chain:b)
   (rel:edge chain:c chain:step-b-c))
(= (chain:finish chain:c)
   (rel:edge chain:d chain:finish-c-d))
(= (chain:loop chain:b)
   (rel:edge chain:b chain:loop-b-b))

(= (chain:path-shape)
   (hyp:path:more (hyp:path:more hyp:path:one)))

(= (chain:examples)
   (hyp:examples
     ((hyp:example chain:a chain:d))
     ((hyp:example chain:a chain:a))))

; Fully-grounded proof checking: the source curried query mode becomes an ordinary
; indexed judgment rather than an untyped proof/theorem pair.
!(type:check
   (hyp:chain chain:sort chain:primitive
     chain:alpha chain:beta chain:delta
     (hyp:primitive chain:sort chain:primitive
       chain:alpha chain:beta chain:encode-symbol)
     (hyp:chain chain:sort chain:primitive
       chain:beta chain:gamma chain:delta
       (hyp:primitive chain:sort chain:primitive
         chain:beta chain:gamma chain:step-symbol)
       (hyp:primitive chain:sort chain:primitive
         chain:gamma chain:delta chain:finish-symbol)))
   (hyp chain:sort chain:primitive
     chain:alpha chain:delta))

; A mismatched middle sort is a checked obstruction in this exact fragment.
!(type:check
   (hyp:chain chain:sort chain:primitive
     chain:alpha chain:gamma chain:delta
     (hyp:primitive chain:sort chain:primitive
       chain:alpha chain:beta chain:encode-symbol)
     (hyp:primitive chain:sort chain:primitive
       chain:gamma chain:delta chain:finish-symbol))
   (hyp chain:sort chain:primitive
     chain:alpha chain:delta))

; Complete raw enumeration has 4^3 occurrences.  Index-directed synthesis
; has the one structurally compatible alpha-to-delta program.
!(list:len
   (hyp:path-candidate-data-untyped
     &self chain:sort chain:primitive
     (chain:path-shape)))
!(list:len
   (hyp:path-candidate-data-typed
     &self chain:sort chain:primitive
     chain:alpha chain:delta
     (chain:path-shape)))

; The source depth bound denotes all shorter proofs too.  Prime reconstructs that
; behavior without weakening the indexed family: the bound enumerates path
; shapes, and every generated program remains an ordinary typed `hyp` value.
!(list:len
   (collapse
     (hyp:path-candidate-typed-within
       &self chain:sort chain:primitive
       chain:alpha chain:delta
       (chain:path-shape))))
!(list:len
   (collapse
     (hyp:path-candidate-typed-within
       &self chain:sort chain:primitive
       chain:alpha chain:alpha
       (chain:path-shape))))

; Forward exploration includes the source occurrence plus every typed path of
; length one through three.  Two authored encode proofs remain two occurrences
; throughout; the delta fibre therefore contains two, not one set-quotiented
; answer.
!(list:len
   (collapse
     (hyp:forward-within
       &self chain:sort chain:primitive
       chain:alpha chain:a
       (chain:path-shape))))
!(list:len
   (collapse
     (hyp:forward-target-within
       &self chain:sort chain:primitive
       chain:alpha chain:delta chain:a
       (chain:path-shape))))

!(hyp:path-benchmark
   chaining-source-intent
   (native-hyp
     (candidate-bag exact)
     (proof-relevant True)
     (intermediate-witnesses retained))
   &self chain:sort chain:primitive
   chain:alpha chain:delta
   (chain:path-shape) (chain:examples))

; Backward use: synthesize a proof program from its endpoint judgment.
!(hyp:learn-path
   &self chain:sort chain:primitive
   chain:alpha chain:delta
   (chain:path-shape) (chain:examples))

; Forward use: execute that checked program from an authored source value.
; Both evidence occurrences reach the same endpoint; the bag remains two
; occurrences.
!(collapse
   (hyp:run
     (quote
       (hyp:chain chain:sort chain:primitive
         chain:alpha chain:beta chain:delta
         (hyp:primitive chain:sort chain:primitive
           chain:alpha chain:beta
           chain:encode-symbol)
         (hyp:chain chain:sort chain:primitive
           chain:beta chain:gamma chain:delta
           (hyp:primitive chain:sort chain:primitive
             chain:beta chain:gamma
             chain:step-symbol)
           (hyp:primitive chain:sort chain:primitive
             chain:gamma chain:delta
             chain:finish-symbol))))
     chain:a))

; The same kernel also admits dependent proof construction, which was one of
; the motivating limits of the original schema-unification prototypes.
!(type:check
   (lam x (refl x))
   (-> (x : u0) (id u0 x x)))
!(type:check
   (lam (f x) (pair x (f x)))
   (-> (f : (-> (x : u0) (id u0 x x)))
       (x : u0)
       (sigma (y : u0) (id u0 x y))))
