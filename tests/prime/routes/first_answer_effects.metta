; These controls require `once` to compute only what its first answer needs:
; with and without --fuel the same effects run, each once, and nothing after
; the first answer is computed.

; Only the effects on the path to the first answer run.
(= (tree 0) leaf)
(= (tree $n)
   (if (> $n 0) (s (let $x (println! (left $n)) (tree (- $n 1))){$x}) (empty)))
(= (tree $n)
   (if (> $n 0) (t (let $x (println! (right $n)) (tree (- $n 1))){$x}) (empty)))
!(once (tree 3))

; A failing alternative gives way to the next; the one after the answer is
; never tried.
(= (pick) (let $x (println! (attempt a)) (empty)))
(= (pick) (let $x (println! (attempt b)) b))
(= (pick) (let $x (println! (attempt c)) c))
!(once (pick))

; A `let` takes its source's values one at a time.
(= (num) (superpose (1 2 3)))
(= (first-above-one)
   (let $n (num)
     (if (> $n 1)
         (let $y (println! (accept $n)) $n){$y}
         (let $y (println! (reject $n)) (empty)){$y})))
!(once (first-above-one))

; So does an argument that must be evaluated before the call.
(= (noisy) (let $x (println! (produce 1)) 1))
(= (noisy) (let $x (println! (produce 2)) 2))
(= (plus-ten) (+ 10 (noisy)))
!(once (plus-ten))

; A search with 2^30 answers stops at its first.
(= (gen 0) z)
(= (gen $n) (if (> $n 0) (s (gen (- $n 1))) (empty)))
(= (gen $n) (if (> $n 0) (t (gen (- $n 1))) (empty)))
(= (depth z) 0)
(= (depth (s $x)) (+ 1 (depth $x)))
(= (depth (t $x)) (+ 1 (depth $x)))
!(depth (once (gen 30)))

; Ready alternatives still have consumers. Literal and computed-equal
; sources expose the same effect prefix; let* has the same demand boundary.
!(once (let $x (superpose (1 2 3)) (println! (literal $x))))
!(once (let $x (superpose ((+ 0 1) 2 3)) (println! (computed $x))))
!(once (let* (($x (superpose (1 2 3)))) (println! (sequential $x))))
!(select 2 (let $x (superpose (1 2 3))
             (chain (println! (prefix $x)) $done $x)))
!(collapse (let $x (superpose (1 2 3))
              (chain (println! (complete $x)) $done $x)))

; A failed consumer demands the next source value; no later value is needed.
!(once (let $x (superpose (1 2 3))
         (chain (println! (guard $x)) $done
           (if (> $x 1) $x (empty)))))

; A source can be a named cell or a captured argument, not just a call.
; Both forms pass the same first-answer demand through their force receiver.
(= choices (chain (println! (symbol first)) $u 1))
(= choices (chain (println! (symbol second)) $u 2))
!(once choices)
(= (first-source $source) (once $source))
(= (offered) (chain (println! (captured first)) $u A))
(= (offered) (chain (println! (captured second)) $u B))
!(first-source (offered))

; A prefix counts occurrences. Equal answers do not erase a production;
; complete collection still demands the last alternative and its effect.
(= (same-answer) (chain (println! (duplicate first)) $u A))
(= (same-answer) (chain (println! (duplicate second)) $u A))
(= (same-answer) (chain (println! (duplicate third)) $u B))
!(select 2 (same-answer))
!(collapse (same-answer))

; A computed named RHS produces its constructor without demanding unused
; fields. Distinct named alternatives still cache independently per branch.
(= (unused-field) (chain (println! forbidden-field) $u unused))
(= lazy-choice (if True (Box (unused-field)) none))
(= lazy-choice (chain (println! forbidden-later) $u (Box later)))
(= (box-tag (Box $x)) box)
!(once (box-tag lazy-choice))
(= left-source (if True A none))
(= right-source (if True B none))
(= shared-source left-source)
(= shared-source right-source)
(= (same-pair $x) (Pair $x $x))
!(collapse (same-pair shared-source))

; Several demanding rules retain rule-major native order. Streaming a
; value through every rule first would incorrectly put (B 1) before (A 2).
(= order-source (Box 1))
(= order-source (Box 2))
(= (ordered-tag (Box $x)) (A $x))
(= (ordered-tag (Box $x)) (B $x))
!(collapse (ordered-tag order-source))
!(select 2 (ordered-tag order-source))

; Equivalent rule heads observe one source episode. The leading rule runs
; one earned row at a time; later rules retain their own matched bodies.
; Renaming a rule's locals does not change that observation program.
(= cohort-input (chain (println! (cohort-read 1)) $u (Box 1)))
(= cohort-input (chain (println! (cohort-read 2)) $u (Box 2)))
(= (cohort-report (Box $x))
   (chain (println! (cohort-A $x)) $u (A $x)))
(= (cohort-report (Box $renamed))
   (chain (println! (cohort-B $renamed)) $u (B $renamed)))
!(once (cohort-report cohort-input))
!(select 2 (cohort-report cohort-input))
!(select 3 (cohort-report cohort-input))
!(select 5 (cohort-report cohort-input))
!(collapse (cohort-report cohort-input))

; An empty first body still matched. Exhaust its source before trying the
; second rule, with no fallback call and no repeated source production.
(= (cohort-empty (Box $x)) (empty))
(= (cohort-empty (Box $y)) (Right $y))
!(once (cohort-empty cohort-input))

; Duplicate rules remain separate occurrences even with identical heads and
; bodies. Already-ready rows retain their source order under finite demand.
(= (cohort-duplicate (Box $x)) (D $x))
(= (cohort-duplicate (Box $x)) (D $x))
!(select 3 (cohort-duplicate order-source))

; An unmatched row is one residual, after the matched plans, even though
; both candidates inspected it. It is not an early answer to `once`.
(= cohort-mixed (Other 0))
(= cohort-mixed (Box 1))
(= (cohort-tag (Box $x)) (A $x))
(= (cohort-tag (Box $y)) (B $y))
!(once (cohort-tag cohort-mixed))
!(select 5 (cohort-tag cohort-mixed))

; A source fault is a delivered occurrence too. It stops `once` before the
; next source effect, and complete demand reports it once, not once per rule.
(= cohort-fault (Error bad SourceFault))
(= cohort-fault (chain (println! after-source-fault) $u (Box 2)))
!(once (cohort-tag cohort-fault))
!(select 4 (cohort-tag cohort-fault))

; Reading a header need not validate its deferred field. A later validator
; can fail without erasing the header or running the fault's diagnostic.
(= (inspection-field) (Error (println! diagnostic-is-data) InspectionFault))
(= (inspection-report (Box $x)) HeaderReady)
(= (inspection-report (Box 7)) DetailReady)
!(once (inspection-report (Box (inspection-field))))
!(select 3 (inspection-report (Box (inspection-field))))
!(collapse (inspection-report (Box (inspection-field))))

; Two successful validations remain two occurrences; they do not multiply
; the independent header that was already available before inspection.
(= (inspection-choices) (superpose (7 7)))
!(collapse (inspection-report (Box (inspection-choices))))
