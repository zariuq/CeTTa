; Use the pinned upstream engine already maintained by the chainer ladder.
!(include ../../../benchmarks/chaining/nil_current/engines/obc)
!(collapse (obc 0 (: $proof (→ A (→ B A)))))
!(collapse (obc 1 (: $proof (→ A (→ B A)))))
!(collapse (obc 4 (: $proof (→ A A))))
!(collapse (obc 5 (: $proof (→ A A))))
