; Compare percent operators with locally authored descriptive policies.
; These adapters use the same native query/constraint services.
!(import! &self pat)

; Atom inputs retain syntax; the result type permits executing the adapter body.
(: query-with (-> %Undefined% Atom Atom %Undefined%))
(= (query-with $space (protect stored) $query)
   (pat:query $space match% $query))
(= (query-with $space (protect query) $query)
   (pat:query $space %match $query))
(: solve-with (-> Atom Atom %Undefined%))
(= (solve-with (protect right) $pairs) (pat:solve match% $pairs))
(= (solve-with (protect left) $pairs) (pat:solve %match $pairs))

(edge $stored b)
(edge a b)
(edge a b)

; Only the two ground rows fit while stored variables stay protected.
!(collapse
   (pat:match% &self (edge a $y) (destination $y)))
!(collapse
   (let (pat:hit $rows $w) (query-with &self (protect stored) (edge a $y))
     (pat:apply $w (destination $y))))

; The converse returns original patterns; the query witness instantiates them.
!(collapse
   (pat:%match &self (edge a b) $stored $stored))
!(collapse
   (let (pat:hit ((pat:row $occurrence $original $view)) $w)
        (query-with &self (protect query) (edge a b))
     (pat:apply $w $original)))

; The descriptive batch form also preserves attempt-wide protection.
!(pat:apply
   (solve-with (protect right) (((P $x) (P a)) ((Q $x) (Q a))))
   (covered $x))
!(pat:apply
   (solve-with (protect left) (((P a) (P $x)) ((Q a) (Q $x))))
   (covered $x))
!(collapse
   (solve-with (protect right) (((P $x) (P $subject)) ((Q $x) (Q a)))))
!(collapse
   (solve-with (protect left) (((P $subject) (P $x)) ((Q a) (Q $x)))))
