; A nested match whose space is bound by an earlier leg reads that space.

!(bind! &other (new-space))
!(add-atom &other (fact good))
!(add-atom &other (deeper inside))
(fact wrong)
(deeper outside)
(spaceref &other)

; The inner space is the outer row's binding.
!(match &self (spaceref $s) (match $s (fact $x) $x))
; The same read through let.
!(match &self (spaceref $s) (let $sp $s (match $sp (fact $x) $x)))
; A literal inner space is known before the chain runs.
!(match &self (spaceref $s) (match &other (fact $x) $x))
; Three legs: the middle leg's space comes from the first leg.
!(collapse (match &self (spaceref $s) (match $s (fact $x) (match &self (deeper $w) ($x $w)))))
; An inner self reference still joins the chain.
!(collapse (match &self (fact $x) (match &self (deeper $w) ($x $w))))

; Rows that select different spaces, a computed space, an unbound space,
; a static cross-space chain and an insertion between read windows.
!(bind! &red (new-space))
!(bind! &blue (new-space))
!(add-atom &red (item r1))
!(add-atom &blue (item b1))
!(add-atom &blue (item b2))
!(add-atom &self (item self1))
!(add-atom &self (pick a &red))
!(add-atom &self (pick b &blue))
(= (space-of red) &red)
(= (space-of blue) &blue)
!(collapse (match &self (pick $k $s) (match $s (item $x) ($k $x))))
!(collapse (match &self (pick a $s) (match (space-of blue) (item $x) $x)))
!(collapse (match &self (pick a $s) (match $unbound (item $x) $x)))
!(collapse (match &red (item $x) (match &blue (item $y) ($x $y))))
!(collapse (match &self (pick $k $s) (let $added (add-atom $s (item late)) (match $s (item $x) ($k $x)))))
