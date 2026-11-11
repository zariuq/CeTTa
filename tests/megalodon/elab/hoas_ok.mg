(* Parameter Bind "73c9efe869770ab42f7cde0b33fe26bbc3e2bd157dad141c0c27d1e7348d60f5" *)
Parameter Bind : (set -> set) -> set.
(* Parameter Pair "d58762d200971dcc7f1850726d9f2328403127deeba124fc3ba2d2d9f7c3cb8c" *)
Parameter Pair : set -> set -> set.
Definition s : set := "Az".
Definition b : set := \x7e.
Definition p : set := Pair s (Bind (fun y => Pair y b)).
Theorem T : forall Q : set -> prop, Q s -> Q b -> Q p -> Q "".
admit.
Qed.
