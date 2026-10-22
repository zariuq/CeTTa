!(import! &self ../../../lib/native_search_goals.metta)
!(import! &self ../../../lib/native_weight_algebras.metta)
!(pragma! prefer-rationals True)

(= (routes) (weight:where (Route a (0) 8) 2))
(= (routes) (weight:where (Route b (0) 1) (/ 1 2)))
(= (routes) (weight:where (Route c (1) 2) 0))
(= (routes) (weight:where (Route d (2) 3) 3))
(= (routes) (weight:where (Route e (3) 4) 4))
(= (routes) (weight:where (Route f (4) 5) 5))
(= (routes) (weight:where (Route g (5) 6) 6))
(= (routes) (weight:where (Route h (6) 7) 7))

!(select 7 (search-policy (control FIFO (budget 200000)))
   (weight:read (weights:rational) (routes)))

!(select 7 (search-policy (control WO
     (best (Weighted (Route $id $coverage $cost) $coefficient $provenance) $cost)
     (budget 200000)))
   (weight:read (weights:rational) (routes)))

!(select 7 (search-policy (control WO
     (key (Route $id $coverage $cost) $cost)
     (goal $selected (search:joint? (0 1 2 3 4 5 6) ((a h)) (weights:values $selected)))
     (budget 250000)))
   (weight:read (weights:rational) (routes)))
