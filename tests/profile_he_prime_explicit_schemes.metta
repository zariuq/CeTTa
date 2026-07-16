; Open declarations are rigid unless explicitly admitted as type schemes.
(: RigidWitness (RigidP $x))
(: SchemeWitness (SchemeP $x))
(type-scheme SchemeWitness)

!(search-first-inhabitant &self (RigidP Nat) 1 10000)
!(search-first-inhabitant &self (SchemeP Nat) 1 10000)

; Query and elaboration substitutions are separate, versioned receipt fields.
!(search-inhabitants &self (SchemeP $query) 1 10000 2)
