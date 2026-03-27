; Classification probes still work on direct IEEE float results.
; Domain-invalid real math is covered separately by test_math_domain_errors.

!(/ 0.0 0.0)
!(/ 5.0 0.0)
!(let $nan (/ 0.0 0.0) (isnan-math $nan))
!(let $inf (/ 5.0 0.0) (isinf-math $inf))
