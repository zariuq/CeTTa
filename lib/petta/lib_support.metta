; Maintained derived spaces.  A view is a space holding its base facts and
; every atom a set of exec directives derives from them, kept so as base facts
; are added and retracted.  A derived atom stays exactly while it has a
; well-founded derivation from the current base facts: an atom never stays
; because of a cycle of derivations alone (a role inherited round a cycle of
; roles is lost when the membership it rested on goes).
;
;   (support:open VIEW DIRECTIVES)   VIEW's atoms become its base facts and VIEW
;                                    is saturated; answers the view's handle
;   (support:assert H A)             A becomes a base fact: (added (B ...)), the
;                                    atoms new in the view, in no particular order
;   (support:retract H A)            A is no longer a base fact: (removed (B ...)),
;                                    the atoms left without a well-founded
;                                    derivation, removed from the view
;   (support:why H A)                (base) for a base fact; (derived KEY (B ...)),
;                                    a derivation of A by the directive KEY from
;                                    atoms of the view; () for an atom not in it
;   (support:space H)                the view
;
; DIRECTIVES are exec directives, (exec KEY (, P ...) (, T ...)), as exec:saturate
; reads them: every match of the query P ... derives the templates T ....  The
; handle records base facts and derivations as data in a space of its own:
; (support:base A) and (support:just KEY HEAD (B ...)).  This library states the
; meaning by recomputing the founded atoms on every retraction; a realization may
; keep them incrementally.

(= (support:space (support:view $view $log $directives)) $view)

(= (support:open $view $directives)
   (let $log (new-space)
     (let $_b (collapse (match $view $a (add-atom $log (support:base $a))))
       (let $_s (support:saturate $view $log $directives)
         (support:view $view $log $directives)))))

; Rounds until one adds nothing, to the view or to the derivations recorded: each
; round proposes every template instance of every directive over every match of
; its query, recording the derivation, then publishes the proposals without
; duplicates.  (A derivation recorded before, whose head a retraction removed,
; publishes the head again.)
(= (support:saturate $view $log $directives)
   (let $before (+ (support:size $view) (support:size $log))
     (let $_r (support:round $view $log $directives)
       (if (== $before (+ (support:size $view) (support:size $log)))
           ()
           (support:saturate $view $log $directives)))))

(= (support:size $space) (size-atom (collapse (get-atoms $space))))

(= (support:round $view $log $directives)
   (let $proposals
        (collapse
          (let (exec $key $query $templates) (superpose $directives)
            (match $view $query
              (let* (($body (cdr-atom $query))
                     ($ts (cdr-atom $templates))
                     ($t (superpose $ts)))
                (support:just $key $t $body)))))
     (support:publish $view $log $proposals)))

(= (support:publish $view $log $proposals)
   (if (== $proposals ()) ()
     (let ($j $more) (decons-atom $proposals)
       (let (support:just $key $head $body) $j
         (let* (($_j (support:add-new $log $j))
                ($_v (support:add-new $view $head)))
           (support:publish $view $log $more))))))

(= (support:assert (support:view $view $log $directives) $a)
   (let $before (collapse (get-atoms $view))
     (let* (($_b (support:add-new $log (support:base $a)))
            ($_v (support:add-new $view $a))
            ($_s (support:saturate $view $log $directives)))
       (let $after (collapse (get-atoms $view))
         (added (support:minus $after $before))))))

(= (support:retract (support:view $view $log $directives) $a)
   (let $_r (remove-atom $log (support:base $a))
     (let $founded (support:founded $log (support:bases $log))
       (let $gone (support:minus (collapse (get-atoms $view)) $founded)
         (let $_d (support:remove-all $view $gone)
           (removed $gone))))))

(= (support:why (support:view $view $log $directives) $a)
   (if (== (collapse (match $view $a $a)) ())
       ()
       (if (== (collapse (match $log (support:base $a) found)) ())
           (let $founded (support:founded $log (support:bases $log))
             (once (match $log (support:just $key $a $body)
                     (if (support:all-in $body $founded) (derived $key $body) (empty)))))
           (base))))

; an atom added unless the space holds it already
(= (support:add-new $space $a)
   (if (== (collapse (match $space $a found)) ()) (add-atom $space $a) ()))

;; ---- the founded atoms --------------------------------------------------------
; The least set holding every base fact and the head of every recorded
; derivation all of whose body atoms it holds.

(= (support:bases $log) (collapse (match $log (support:base $a) $a)))

(= (support:founded $log $known)
   (let $more (collapse (match $log (support:just $key $head $body)
                          (if (and (support:all-in $body $known)
                                   (not (is-alpha-member $head $known)))
                              $head
                              (empty))))
     (if (== $more ())
         $known
         (support:founded $log (support:union $more $known)))))

(= (support:all-in $xs $set)
   (if (== $xs ()) True
     (let ($x $more) (decons-atom $xs)
       (if (is-alpha-member $x $set) (support:all-in $more $set) False))))

(= (support:union $xs $set)
   (if (== $xs ()) $set
     (let ($x $more) (decons-atom $xs)
       (support:union $more (if (is-alpha-member $x $set) $set (cons-atom $x $set))))))

; the atoms of XS not in SET, each once
(= (support:minus $xs $set)
   (if (== $xs ()) ()
     (let ($x $more) (decons-atom $xs)
       (let $tail (support:minus $more $set)
         (if (or (is-alpha-member $x $set) (is-alpha-member $x $tail))
             $tail
             (cons-atom $x $tail))))))

(= (support:remove-all $space $atoms)
   (if (== $atoms ()) ()
     (let ($a $more) (decons-atom $atoms)
       (let $_r (remove-atom $space $a) (support:remove-all $space $more)))))
