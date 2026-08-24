; Prime keeps proof-grade checking separate from open-world analysis.

(: Nat Type)
(: Bool Type)
(: Z Nat)
(: Truth Bool)
(: unknown %Undefined%)

!(type:check Z Nat)
!(type:check Truth Nat)
!(type:analyze unknown Nat)
!(type:check unknown Nat)
