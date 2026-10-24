; ---- Prime's own strategy: `let` is the eager `to`, call arguments are
; shared thunks.
(= (pick) heads)
(= (pick) tails)
(= (k $a $b) $a)
(= (twice $x) (P $x $x))
!(let $c (pick) done)
!(k one (pick))
!(twice (pick))

; ---- native bounded observation: the first report is already available.
; Asking for it must not request a sensor used only by a later rule.
; Asking for one more report requests A, leaving B and C untouched.
(= (report-input) (Box A (report-sensor A)))
(= (report-input) (Box B (report-sensor B)))
(= (report-input) (Box C (report-sensor C)))
(= (report-sensor $tag) (chain (println! (SensorRequested $tag)) $effect 7))
(= (native-report (Box $tag $reading)) Ready)
(= (native-report (Box $tag 7)) $tag)
!(once (native-report (report-input)))
!(select 4 (native-report (report-input)))
