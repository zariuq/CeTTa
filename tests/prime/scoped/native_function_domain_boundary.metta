; Function domains are retained by native checking and conversion.
; These are native declarations, not assertions about set-code equality.
!(bind! &thy (new-space))
!(add-atom &thy (: E (u 0)))
!(add-atom &thy (: Truth (u 0)))
!(type:formed &thy (-> E E))
!(type:formed &thy (-> Truth (-> E E)))
!(type:eq &thy (-> E E) (-> Truth (-> E E)))
!(type:check &thy (lam x x) (-> E E))
!(type:check &thy (lam x x) (-> Truth (-> E E)))
!(type:check &thy (lam p (lam x x)) (-> Truth (-> E E)))
; The checked beta computation preserves the original function domain.
!(set:define &thy applyIdentity (-> (-> E E) E E)
   (= (applyIdentity $f $x) ($f $x)))
!(add-atom &thy (: e E))
!(add-atom &thy (: other E))
!(type:check &thy (applyIdentity (lam x x) e) E)
!(type:check &thy (applyIdentity (lam p (lam x x)) e) E)
!(type:eq &thy (applyIdentity (lam x x) e) e)
!(type:eq &thy (applyIdentity (lam x x) other) other)
!(type:eq &thy (applyIdentity (lam x x) other) e)
; An undeclared body remains unresolved rather than becoming evidence.
!(let $outcome (try (type:check &thy (lam x missingBody) (-> E E)))
   (car-atom $outcome))
; Exhaustion is not a negative result; ordinary checking is unbounded.
!(type:check &thy (applyIdentity (lam x x) e) E 1)
!(type:check &thy (applyIdentity (lam x x) e) E)
