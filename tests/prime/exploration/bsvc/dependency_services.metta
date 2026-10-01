; Dependency services over a package catalogue, each a total loop with a
; variant, and a watcher that applies them to every snapshot of a stream.
;
; A catalogue lists its items and, for each item, the items it depends on.
;   packages    core; text on core; json on text, core; http on json, text;
;               db on core; cli on text; app on http, db, cli
;   cyclic      the same, except that cli depends on text and app
;   self-loop   one item, core, depending on itself
; x uses y when y is among x's dependencies.
;
; Services:
;   affected    the changed items and everything that depends on one of them,
;               directly or not: a reverse sweep that adds, in catalogue
;               order, the unseen items listing a frontier item, until none
;               is added (variant: the unseen items);
;   schedule    rebuild waves by Kahn's algorithm: a wave is the pending
;               targets none of whose dependencies is pending, in target
;               order; if pending targets remain and none is ready, they are
;               reported as blocked (variant: the pending targets);
;   cycle       the schedule of the whole catalogue; when it is blocked, a
;               walk inside the blocked set that follows the first dependency
;               in the set until an item repeats, closed at that item;
;   fold        with an acyclicity certificate (the flattened waves of the
;               whole catalogue), a value per item computed in that order from
;               the item and its dependencies' values, in the order the
;               catalogue lists them: derivation keys and critical-path
;               lengths;
;   watcher     for each snapshot of a stream of catalogues, the schedule of
;               the items affected by what changed since the previous one.
; A derivation key combines an item's content with its dependencies' keys:
; key = fold of (acc * 131 + k) mod 1000003 over the dependency keys, from the
; content.  Contents: core 11, text 23, json 37, http 41, db 53, cli 67,
; app 79; the edit changes text to 24.
;
; Mirrors: mem_affected, db_not_uses_text, schedule_spec, Respects.acyclic,
; findCycle_spec, cycle?_sound, cycle?_isSome_iff, acyclic?_isSome_iff,
; depFold_eq, depFold_eq_of_not_affected, key_db_unchanged,
; rebuildWatch_spec, and the computed values of the package-catalogue
; examples.
; Values are bound with let before a list builtin reads them: a builtin that
; takes an atom reads an expression written in its argument as syntax.

; ---- lists ------------------------------------------------------------------------

(= (in-list $x $xs)
   (if (== $xs ()) False
       (let $h (car-atom $xs)
         (if (== $h $x) True (let $t (cdr-atom $xs) (in-list $x $t))))))
(= (append $xs $ys)
   (if (== $xs ()) $ys
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)) ($r (append $t $ys))) (cons-atom $h $r))))
(= (intersects $xs $ys)
   (if (== $xs ()) False
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)))
         (if (in-list $h $ys) True (intersects $t $ys)))))
(= (first-occurrences $xs)
   (if (== $xs ()) ()
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)) ($r (first-occurrences $t))
              ($r2 (filter-atom $r $y (not (== $y $h)))))
         (cons-atom $h $r2))))
(= (flatten $xss) (foldl-atom $xss () $acc $xs (append $acc $xs)))
(= (lookup $x $pairs)
   (if (== $pairs ()) missing
       (let* ((($k $v) (car-atom $pairs)) ($rest (cdr-atom $pairs)))
         (if (== $k $x) $v (lookup $x $rest)))))

; ---- the catalogues ----------------------------------------------------------------

(= (items packages) (core text json http db cli app))
(= (deps packages core) ())
(= (deps packages text) (core))
(= (deps packages json) (text core))
(= (deps packages http) (json text))
(= (deps packages db) (core))
(= (deps packages cli) (text))
(= (deps packages app) (http db cli))
(= (items cyclic) (core text json http db cli app))
(= (deps cyclic $x) (if (== $x cli) (text app) (deps packages $x)))
(= (items self-loop) (core))
(= (deps self-loop $x) (core))

; ---- affected items ------------------------------------------------------------------

(= (affected $cat $changed) (sweep $cat $changed $changed))
(= (sweep $cat $seen $frontier)
   (let* (($items (items $cat))
          ($fresh (filter-atom $items $x
                    (and (not (in-list $x $seen)) (let $ds (deps $cat $x) (intersects $frontier $ds))))))
     (if (== $fresh ()) $seen
         (let $seen2 (append $seen $fresh) (sweep $cat $seen2 $fresh)))))

; ---- rebuild waves ------------------------------------------------------------------

(= (schedule $cat $target) (let $rem (first-occurrences $target) (kahn $cat () $rem)))
(= (kahn $cat $layers $rem)
   (if (== $rem ()) (waves $layers)
       (let $ready (filter-atom $rem $x (let $ds (deps $cat $x) (not (intersects $ds $rem))))
         (if (== $ready ()) (blocked $rem)
             (let* (($layers2 (append $layers ($ready)))
                    ($rem2 (filter-atom $rem $x (not (in-list $x $ready)))))
               (kahn $cat $layers2 $rem2))))))
; An order respects the dependencies: each item once, and no item depends on
; itself or on a later item.
(= (respects $cat $order)
   (if (== $order ()) True
       (let* (($a (car-atom $order)) ($later (cdr-atom $order)) ($ds (deps $cat $a)))
         (and (not (in-list $a $later))
              (and (not (in-list $a $ds))
                   (and (not (intersects $later $ds)) (respects $cat $later)))))))

; ---- cycles ----------------------------------------------------------------------------

(= (first-member $xs $set)
   (if (== $xs ()) none
       (let* (($h (car-atom $xs)) ($t (cdr-atom $xs)))
         (if (in-list $h $set) $h (first-member $t $set)))))
; The segment of a reversed walk from its head down to y, in walk order.
(= (cycle-at $y $path)
   (if (== $path ()) ()
       (let* (($a (car-atom $path)) ($l (cdr-atom $path)))
         (if (== $a $y) ($a) (let $r (cycle-at $y $l) (append $r ($a)))))))
(= (walk $cat $B $path)
   (let* (($x (car-atom $path)) ($ds (deps $cat $x)) ($y (first-member $ds $B)))
     (if (== $y none) ()
         (if (in-list $y $path) (cycle-at $y $path)
             (let $p2 (cons-atom $y $path) (walk $cat $B $p2))))))
(= (find-cycle $cat $B) (if (== $B ()) () (let $x (car-atom $B) (walk $cat $B ($x)))))
(= (cycle $cat)
   (let* (($items (items $cat)) ($s (schedule $cat $items)))
     (unify $s (blocked $B) (let $w (find-cycle $cat $B) (some $w)) none)))
; A closed walk along dependencies.
(= (closed-walk $cat $w)
   (let $first (car-atom $w) (steps-along $cat $w $first)))
(= (steps-along $cat $w $first)
   (let* (($x (car-atom $w)) ($rest (cdr-atom $w)) ($ds (deps $cat $x)))
     (if (== $rest ()) (in-list $first $ds)
         (let $y (car-atom $rest) (and (in-list $y $ds) (steps-along $cat $rest $first))))))
(= (acyclic $cat)
   (let* (($items (items $cat)) ($s (schedule $cat $items)))
     (unify $s (waves $layers) (let $o (flatten $layers) (some $o)) none)))

; ---- folds over dependencies ------------------------------------------------------------

(= (dep-fold $cat $f)
   (let $cert (acyclic $cat)
     (unify $cert (some $order) (fold-in-order $cat $f $order ()) (no-certificate $cat))))
(= (fold-in-order $cat $f $order $done)
   (if (== $order ()) $done
       (let* (($x (car-atom $order)) ($rest (cdr-atom $order)) ($ds (deps $cat $x))
              ($vs (map-atom $ds $y (lookup $y $done)))
              ($v ($f $x $vs)) ($done2 (append $done (($x $v)))))
         (fold-in-order $cat $f $rest $done2))))
(= (content base core) 11)
(= (content base text) 23)
(= (content base json) 37)
(= (content base http) 41)
(= (content base db) 53)
(= (content base cli) 67)
(= (content base app) 79)
(= (content edited $x) (if (== $x text) 24 (content base $x)))
(= (combine $c $x $ks)
   (let $start (content $c $x) (foldl-atom $ks $start $acc $k (% (+ (* $acc 131) $k) 1000003))))
(= (key-base $x $ks) (combine base $x $ks))
(= (key-edited $x $ks) (combine edited $x $ks))
(= (height $x $hs) (+ (foldl-atom $hs 0 $m $h (if (< $m $h) $h $m)) 1))

; ---- the watcher ---------------------------------------------------------------------------

(= (changed-from $old $new)
   (let* (($items (items $new)) ($olds (items $old)))
     (filter-atom $items $x
       (or (not (in-list $x $olds)) (let* (($a (deps $old $x)) ($b (deps $new $x))) (not (== $a $b)))))))
(= (rebuild-round $old $new)
   (let* (($ch (changed-from $old $new)) ($aff (affected $new $ch))) (schedule $new $aff)))
(= (watch $prev $snapshots)
   (if (== $snapshots ()) ()
       (let* (($new (car-atom $snapshots)) ($rest (cdr-atom $snapshots))
              ($out (rebuild-round $prev $new)) ($outs (watch $new $rest)))
         (cons-atom $out $outs))))

; ---- affected items ------------------------------------------------------------------

; 1. An edit to text affects text, json, http, cli and app; one to db affects
;    db and app (mem_affected).
!(affected packages (text))
!(affected packages (db))
; 2. db does not depend on text (db_not_uses_text).
!(let $a (affected packages (text)) (in-list db $a))

; ---- rebuild waves --------------------------------------------------------------------

; 3. The waves of the whole catalogue, and of the rebuild after an edit to text
;    (schedule_spec).
!(let $is (items packages) (schedule packages $is))
!(let $a (affected packages (text)) (schedule packages $a))
; 4. The flattened waves respect the dependencies, and an order that builds
;    app first does not (schedule_spec, Respects).
!(let* (($is (items packages)) ((waves $ws) (schedule packages $is)) ($o (flatten $ws)))
   ((respects packages $o) (respects packages (app core text json http db cli))))

; ---- cycles ----------------------------------------------------------------------------

; 5. The catalogue has no cycle, and its flattened waves are an acyclicity
;    certificate (cycle?_isSome_iff, acyclic?_isSome_iff).
!(cycle packages)
!(acyclic packages)
; 6. In the cyclic variant the schedule is blocked at cli and app, the cycle
;    found is cli, app, it is a closed walk along dependencies, and there is no
;    certificate (schedule_spec, findCycle_spec, cycle?_sound, Respects.acyclic).
!(let $is (items cyclic) (schedule cyclic $is))
!(cycle cyclic)
!(closed-walk cyclic (cli app))
!(acyclic cyclic)
; 7. A self-dependent item is a cycle of length one.
!(cycle self-loop)

; ---- folds over dependencies ----------------------------------------------------------------

; 8. Derivation keys: core 11, text 3024, app 529788; critical-path lengths:
;    core 1, app 5 (depFold_eq).
!(let $ks (dep-fold packages key-base) ((lookup core $ks) (lookup text $ks) (lookup app $ks)))
!(let $hs (dep-fold packages height) ((lookup core $hs) (lookup app $hs)))
; 9. Incremental rebuild: after the edit to text, db's key is unchanged and
;    app's key is 151915 (key_db_unchanged) ...
!(let* (($k0 (dep-fold packages key-base)) ($k1 (dep-fold packages key-edited)))
   ((== (lookup db $k1) (lookup db $k0)) (lookup app $k1) (== (lookup app $k1) (lookup app $k0))))
; ... and every item outside the affected set keeps its key
;     (depFold_eq_of_not_affected).
!(let* (($k0 (dep-fold packages key-base)) ($k1 (dep-fold packages key-edited))
        ($a (affected packages (text))) ($is (items packages))
        ($outside (filter-atom $is $x (not (in-list $x $a)))))
   ($outside (foldl-atom $outside True $ok $x (and $ok (== (lookup $x $k1) (lookup $x $k0))))))
; 10. No certificate, no fold: the cyclic variant is refused.
!(dep-fold cyclic key-base)

; ---- the watcher ------------------------------------------------------------------------------

; 11. Snapshots: the catalogue, the cyclic variant, the catalogue, the
;     catalogue.  Each round schedules what that round's change affects, or
;     reports it blocked (rebuildWatch_spec).
!(watch packages (packages cyclic packages packages))
