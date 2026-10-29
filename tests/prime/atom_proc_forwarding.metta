; A value parameter stays computational through an untyped caller, including
; sources with local logic variables and a consumer that holds data.
(scope:profile mercury-implicit per-call reference)
(: forwarding:hold (-> Atom Atom))
(= (forwarding:hold $x) $x)
(: forwarding:value (-> Proc Atom))
(= (forwarding:value $x) (forwarding:hold $x))
(= (forwarding:call $x) (forwarding:value $x))
!(forwarding:call (let $local 7 $local))
!(forwarding:call (superpose (1 2)))
!(forwarding:call
   (let $_ (println! "FORWARDED-ONCE") (let $local 8 $local)))
