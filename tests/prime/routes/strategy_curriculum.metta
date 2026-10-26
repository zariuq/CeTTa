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

; A call argument has shared identity without freezing the mutable world
; at call entry. The first force sees earlier writes in its own branch.
(= (read-after-own-write $cell $reading)
   (chain (change-state! $cell 1) $done $reading))
!(let $cell (new-state 0)
   (read-after-own-write $cell (get-state $cell)))
!(let $cell (new-state 0)
   (let $reading (delay (get-state $cell))
     (chain (change-state! $cell 1) $done (force $reading))))

; Once forced, a delay retains that branch's completed value. Explicit
; resampling requests a new reading after the write.
!(let $cell (new-state 0)
   (let $reading (delay (get-state $cell))
     (let $before (force $reading)
       (chain (change-state! $cell 1) $done
         (Pair $before (force $reading))))))
!(let $cell (new-state 0)
   (let $reading (resample (get-state $cell))
     (let $before (force $reading)
       (chain (change-state! $cell 1) $done
         (Pair $before (force $reading))))))

; Different rule branches may make different legitimate writes before
; demanding the same argument. A cell-only global cache would erase this.
(= (world-report $cell $reading)
   (chain (change-state! $cell 1) $done
     (chain $reading $value (Left $value))))
(= (world-report $cell $reading)
   (chain (change-state! $cell 2) $done
     (chain $reading $value (Right $value))))
!(let $cell (new-state 0)
   (collapse (world-report $cell (get-state $cell))))

; Independent rules read the common input. Inspecting the left source for
; the first rule must neither multiply the second rule nor run right twice.
(= (curriculum-left) (chain (println! (Produced Left)) $done (superpose (A X))))
(= (curriculum-right) (chain (println! (Produced Right)) $done B))
(= (curriculum-report A $unused) first)
(= (curriculum-report $unused B) second)
!(collapse (curriculum-report (curriculum-left) (curriculum-right)))

; A matching head covers its input even when its body produces no answers.
; Only X/Y is genuinely uncovered by the union of these two heads.
(= (curriculum-cover A $unused) (superpose ()))
(= (curriculum-cover $unused B) detail)
!(collapse (curriculum-cover (superpose (A X)) (superpose (B Y))))
