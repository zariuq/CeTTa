; A bound past 32 bits takes every answer, not the bound's low bits; the
; answers taken are a bag, read sorted.
!(let $r (select 4294967298 (superpose (a b c))) (sort-atom $r))
!(let $r (select 4294967296 (superpose (a a b))) (sort-atom $r))
!(let $r (select 9223372036854775807 (superpose (a b c))) (sort-atom $r))
(= (wide-pick) (select 4294967298 (superpose (a b c))))
!(let $r (wide-pick) (sort-atom $r))
!(select 0 (println! should-not-run))
