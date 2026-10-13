; The bound of a selection ends its search: once it holds k answers, the
; alternatives after them never run.  A run that took k answers only after
; computing every alternative would print the later effects too.

(= (pick $tag) (let $x (println! ($tag 1)) 1))
(= (pick $tag) (let $x (println! ($tag 2)) 2))
(= (pick $tag) (let $x (println! ($tag 3)) 3))
(= (pick $tag) (let $x (println! ($tag 4)) 4))
!(select 2 (pick two))
!(select 3 (pick three))
!(once (select 2 (pick nested)))
