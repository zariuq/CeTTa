; Under the per-closure lifetime a written quotation's own names move to the
; scope that creates it, as a template's do (stage 5, item 3): at a query
; they are the query's names, so every opening shares them.  Two openings
; that bind them alike agree; two that bind them apart conflict.
(scope:profile mercury-implicit per-closure)
!(import! &self template)

!(row template-map-shared)
!(template:map (1 2) k @(let $u k (Pair $u $u)))
!(row template-map-shared-agree)
!(template:map (1 1) k @(let $u k (Pair $u $u)))
!(row two-instances-shared)
!(let $c @(let $u k $u) (Pair *(lift let k @1 $c) *(lift let k @2 $c)))
!(row two-instances-shared-agree)
!(let $c @(let $u k $u) (Pair *(lift let k @1 $c) *(lift let k @1 $c)))
!(row two-openings-shared)
!(let $c @(Pair $u k) (Pair *$c *$c))
