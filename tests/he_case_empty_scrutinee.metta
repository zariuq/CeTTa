; case switches on Empty when its atom has no results: the first branch
; whose pattern matches Empty is taken, a variable pattern included, and an
; atom with results is switched on each of them.
!(case (superpose ()) ((Empty none) ($x (some $x))))
!(case (match &self (absent $x) $x) ((Empty none) ($x (some $x))))
!(case (let $value (superpose ()) (got $value)) ((Empty none) ($x (some $x))))
!(case (superpose ()) (($x variable)))
!(case (superpose (a b)) ((Empty none) ($x (some $x))))
!(case Empty ((Empty none) ($x (some $x))))
