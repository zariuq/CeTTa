; Explicit infinity and exact rational costs retain duplicate contributions.
!(import! &self ../../../lib/native_weight_algebras.metta)
!(pragma! prefer-rationals True)
(=w (route) A (TropicalFinite (/ 1 3)))
(=w (route) A (TropicalFinite (/ 1 3)))
(=w (route) B TropicalInfinity)
!(weight:read (weights:tropical) (route))
