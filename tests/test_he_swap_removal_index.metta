; Removing from an unordered space moves its last row into the gap; the
; indexes follow both rows.

(item 1 one)
(item 2 two)
(item 3 three)
(item 4 four)
(= (look $k) (match &self (item $k $v) $v))

!(look 2)
!(remove-atom &self (item 2 two))
!(look 2)
!(look 4)
!(let $bag (collapse (match &self (item $k $v) $k)) (sort-atom $bag))

; The last row moves into the gap left by an earlier one.
!(add-atom &self (item 5 five))
!(remove-atom &self (item 1 one))
!(look 5)
!(let $bag (collapse (match &self (item $k $v) ($k $v))) (sort-atom $bag))

; Removing the last row moves nothing.
!(remove-atom &self (item 5 five))
!(look 5)
!(let $bag (collapse (match &self (item $k $v) $k)) (sort-atom $bag))

; A copy stays after one of two equal rows is removed.
!(add-atom &self (item 6 six))
!(add-atom &self (item 6 six))
!(remove-atom &self (item 6 six))
!(let $bag (collapse (match &self (item 6 $v) $v)) (sort-atom $bag))

; An equation that moves into a gap is still found.
(gap 1)
(= (late) 42)
!(remove-atom &self (gap 1))
!(late)

; Removing an absent row changes nothing.
!(remove-atom &self (item 99 none))
!(let $bag (collapse (match &self (item $k $v) $k)) (sort-atom $bag))

; Many removals and additions between queries.
(= (churn $n) (if (== $n 0) done
    (let* (($a (add-atom &self (tick $n)))
           ($b (remove-atom &self (tick $n))))
          (churn (- $n 1)))))
!(churn 40)
!(let $bag (collapse (match &self (tick $n) $n)) (sort-atom $bag))
!(let $bag (collapse (match &self (item $k $v) $k)) (sort-atom $bag))
