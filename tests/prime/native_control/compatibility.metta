!(import! &self ../../../lib/native_search_goals.metta)
(= (routes) (Route tempting (0 1) 0))
(= (routes) (Route necessary (2) 5))
(= (routes) (Route alternate (0 1) 7))
!(select 2
   (search-policy (control WO
      (key (Route $id $coverage $cost) $cost)
      (goal $chosen (search:joint? (0 1 2) ((tempting necessary)) $chosen))
      (budget 100000)))
   (routes))
