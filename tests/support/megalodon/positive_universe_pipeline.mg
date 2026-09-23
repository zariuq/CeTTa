Theorem pipeline_power_closure : forall N:set, Power_closed (UnivOf N).
exact (fun N => UnivOf_ZF_closed N (Power_closed (UnivOf N))
  (fun closed repl => closed (Power_closed (UnivOf N))
    (fun union power => power))).
Qed.

Theorem pipeline_union_closure : forall N:set, Union_closed (UnivOf N).
exact (fun N => UnivOf_ZF_closed N (Union_closed (UnivOf N))
  (fun closed repl => closed (Union_closed (UnivOf N))
    (fun union power => union))).
Qed.

Theorem pipeline_membership : In (Power (Union (Power Empty))) (UnivOf Empty).
exact (pipeline_power_closure Empty (Union (Power Empty))
  (pipeline_union_closure Empty (Power Empty)
    (pipeline_power_closure Empty Empty (UnivOf_In Empty)))).
Qed.
