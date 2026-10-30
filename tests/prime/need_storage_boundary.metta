; Persistent spaces store ordinary Prime syntax, never evaluator-private
; thunk references.  A space holds values: crossing the boundary demands
; the cells the evaluator made in the payload, while syntax the program
; wrote stays data and an explicit suspension stays a suspension.
!(bind! &prime-need:stored (new-space))
!(bind! &prime-need:store-counter (new-state 0))
(= (prime-need:store-bump)
   (let $old (get-state &prime-need:store-counter)
     (let $_ (change-state! &prime-need:store-counter (+ $old 1))
       $old)))
(= (prime-need:store-unused $x)
   (let $_ (add-atom &prime-need:stored (Stored $x)) stored))

!(prime-need:store-unused (prime-need:store-bump))
!(get-state &prime-need:store-counter)
; Storage demands the pending argument once, and the stored row holds its
; value; the ambient state is unchanged, as for every branch receipt.  An
; ordinary read and a declared Data view both retrieve that value, and
; neither view exposes heap IDs.
!(match &prime-need:stored (Stored $saved) $saved)
(: prime-need:stored-code (-> Atom Atom))
(= (prime-need:stored-code $value) $value)
!(match &prime-need:stored (Stored $saved) (prime-need:stored-code $saved))
!(match &prime-need:stored (Stored (__prime_need_ref_v1 $s $t)) leaked-ref)

; An explicit suspension remains explicit across the same data boundary.
!(add-atom &prime-need:stored
   (StoredExplicit (delay (prime-need:store-bump))))
!(match &prime-need:stored (StoredExplicit $saved) $saved)
!(get-state &prime-need:store-counter)

; A registry lookup forwards an earned value; only explicit eval activates
; a callable-shaped value. Nested promises keep their stored identity.
!(bind! &prime-need:registry-data (cons-atom + (1 2)))
!(let $v &prime-need:registry-data (Pair $v (eval $v)))
(= (prime-need:registry-box) (Box (delay (superpose (A B)))))
!(bind! &prime-need:registry-box (prime-need:registry-box))
!(let $b &prime-need:registry-box
   (let $p (index-atom $b 1) (Pair (force $p) (force $p))))

; One shared argument is demanded once per branch, so each branch stores
; one row whose two uses agree: never (Pair A B).  Two independent arguments
; store one row per combination, as eager evaluation would.
(= (prime-need:store-shared $x)
   (add-atom &prime-need:stored (StoredShared (Pair $x $x))))
!(prime-need:store-shared (superpose (A B)))
!(match &prime-need:stored (StoredShared $pair) $pair)
!(match &prime-need:stored (StoredShared $pair)
   (let $left (index-atom $pair 1)
     (let $right (index-atom $pair 2)
       (Pair (eval $left) (eval $right)))))
(= (prime-need:store-independent $x $y)
   (add-atom &prime-need:stored (StoredIndependent $x $y)))
!(prime-need:store-independent (superpose (A B)) (superpose (A B)))
!(match &prime-need:stored (StoredIndependent $x $y)
   (Pair (eval $x) (eval $y)))

; Binding a promise already forced on this branch retains its cache and
; effect state within the selected branch; the next top-level world starts
; from the ambient state. Equal syntax does not merge distinct promises.
!(let $p (delay (prime-need:store-bump))
   (let $x (force $p)
     (let $_ (bind! &prime-need:completed $p)
       (Checks $x (force &prime-need:completed)
               (get-state &prime-need:store-counter)))))
!(get-state &prime-need:store-counter)
!(let $p (delay 7)
   (let $q (delay 7)
     (Checks (== $p $q) (=alpha $p $q)
             (same-suspension $p $q) (same-origin $p $q))))

; Declined prepared admission retains the same canonical source request.
; An invalid quantum must not choose a weaker return representation.
!(pragma! prepared-node-quantum 0)
!(bind! &prime-need:registry-declined (prime-need:registry-box))
!(let $b &prime-need:registry-declined
   (let $p (index-atom $b 1) (Pair (force $p) (force $p))))
!(pragma! prepared-node-quantum 1)
(= (prime-need:registry-nested)
   (let $_ (bind! &prime-need:registry-inner (prime-need:registry-box))
     (let $b &prime-need:registry-inner
       (let $p (index-atom $b 1) (Pair (force $p) (force $p))))))
!(prime-need:registry-nested)

; A stored implicit callable is demanded in call position through its
; retained key. Repeated calls share a choice; different keys stay independent.
(scope:profile mercury-implicit per-call reference)
(= (sc:mk) (lam z (let $y z (g $y))))
(= (sc:factory) (chain (println! sc:made) $unused (superpose ((lam z (Pair A z)) (lam z (Pair B z))))))
(= (sc:box) (Box (sc:factory)))
!(let $f (sc:mk) (add-atom &self (sc:stored $f)))
!(match &self (sc:stored $f) (Pair ($f 1) ($f 2)))
!(let (Box $f) (sc:box) (add-atom &self (sc:choice $f)))
!(match &self (sc:choice $f) (Pair ($f 1) ($f 2)))
!(match &self (sc:choice $f) (Pair $f $f))
!(let (Box $f) (sc:box) (let (Box $g) (sc:box) (add-atom &self (sc:distinct $f $g))))
!(match &self (sc:distinct $f $g) (Pair ($f 1) ($g 2)))

; An already produced callable-shaped datum retains its value role through
; storage and matching. Only explicit activation executes that datum.
!(let $code (cons-atom + (1 2))
   (add-atom &prime-need:stored (CompletedCode $code)))
!(match &prime-need:stored (CompletedCode $code) $code)
!(match &prime-need:stored (CompletedCode $code) (Pair $code (eval $code)))
!(let $code (cons-atom println! (StoredCodeMustRemainData))
   (add-atom &prime-need:stored (CompletedEffect $code)))
!(match &prime-need:stored (CompletedEffect $code) $code)
!(match &prime-need:stored (CompletedEffect $code) (Pair $code $code))

; A second crossing retains the same distinction. Equal stored rows remain
; two occurrences; collecting them does not activate either returned datum.
!(match &prime-need:stored (CompletedCode $code)
   (add-atom &prime-need:stored (CompletedAgain $code)))
!(match &prime-need:stored (CompletedAgain $code) $code)
!(match &prime-need:stored (CompletedAgain $code) (eval $code))
!(add-atom &prime-need:stored (CompletedCode (+ 1 2)))
!(reify (match &prime-need:stored (CompletedCode $code) $code))


; A native payload returns the State from its actual logical world. The
; write remains isolated, and returning the cell must retain its new value.
!(import! &self rhometta)
!(rhometta:values
  (rhometta:run-canonical
    (rho:par
      (rho:send (rho:quote rho:nil)
        (rho:eval-payload
          (quote (let $cell (new-state 0)
                   (chain (change-state! $cell 7) $done $cell)))))
      (rho:recv (rho:quote rho:nil) $answer (rho:drop $answer)))))

; Explicit quotation crossing builds deferred code. If an enclosing caller
; has already produced a State, the service captures that caller's logical
; value before creating its isolated image. Two uses retain one image.  The
; parameters that carry the code are `Atom`: they hold their argument
; as written, so the quotation captures the code.  A value parameter would
; denote the value the code computes, and observing the quotation would run
; it.
(: prime-need:defer (-> Atom Proc))
(= (prime-need:defer $payload) (rho:eval-payload (quote $payload){$payload}))
(: prime-need:run-payload (-> Proc %Undefined%))
(= (prime-need:run-payload $node)
   (rhometta:values
     (rhometta:run-canonical
       (rho:par
         (rho:send (rho:quote rho:nil) $node)
         (rho:recv (rho:quote rho:nil) $answer (rho:drop $answer))))))
(: prime-need:through-rho (-> Atom %Undefined%))
(= (prime-need:through-rho $payload)
   (prime-need:run-payload (prime-need:defer $payload)))
!(prime-need:defer (println! "DeferredCodeMustRemainInert"))
!(prime-need:through-rho
   (let $cell (new-state 0) (chain (change-state! $cell 7) $done $cell)))
!(prime-need:through-rho
   (let $cell (new-state 0) (chain (change-state! $cell 7) $done (Pair $cell $cell))))

; A stored reference is discovered when the payload reads it. Capturing the
; Space alone must not erase the current logical value of its stored State.
!(let $cell (new-state 0)
   (let $store (new-space)
     (chain (add-atom $store $cell) $added
       (chain (change-state! $cell 7) $changed
         (prime-need:run-payload
           (rho:eval-payload (quote (get-state (get-atoms $store))){$store}))))))
!(let $cell (new-state 0)
   (let $store (new-space)
     (chain (add-atom $store $cell) $added
       (chain (change-state! $cell 7) $changed
         (prime-need:run-payload
           (rho:eval-payload
             (quote (let $found (get-atoms $store) (Pair $found $found))){$store}))))))

; Frozen scalar observations and current reads answer different questions.
; Equal contents do not make separate cells aliases. The same program over
; one shared cell observes both reads change; no handle comparison is used.
(= (prime-need:observe-write $left $right)
   (chain (get-state $left) $before-left
     (chain (get-state $right) $before-right
       (chain (change-state! $left 9) $changed
         (chain (get-state $left) $after-left
           (chain (get-state $right) $after-right
             (Comparisons
               (frozen (== $before-left $before-right))
               (current (== $after-left $after-right))
               (values $after-left $after-right))))))))
!(DistinctResources
   (prime-need:observe-write (new-state 7) (new-state 7)))
!(let $cell (new-state 7)
   (AliasedResource (prime-need:observe-write $cell $cell)))

; Restrict the client to its selected cell. Two separate cells now expose
; the same read/write trace; the actual mutation remains observable.
(= (prime-need:observe-selected-write $cell)
   (chain (get-state $cell) $before
     (chain (change-state! $cell 9) $changed
       (chain (get-state $cell) $after
         (Trace $before $after)))))
!(SelectedOnly
   (prime-need:observe-selected-write (new-state 7))
   (prime-need:observe-selected-write (new-state 7)))

; Equal leaf readings do not reveal whether two paths share one descendant.
; Writing through the left path does: the right read changes only in the
; shared diamond.  This client observes values and effects explicitly; it
; does not assume a meaning for equality between live resource handles.
(= (prime-need:observe-diamond $left $right)
   (chain (get-state $left) $left-leaf
     (chain (get-state $right) $right-leaf
       (chain (get-state $left-leaf) $before-left
         (chain (get-state $right-leaf) $before-right
           (chain (change-state! $left-leaf 9) $changed
             (chain (get-state $left-leaf) $after-left
               (chain (get-state $right-leaf) $after-right
                 (Diamond
                   (before $before-left $before-right)
                   (after $after-left $after-right))))))))))
!(let $leaf (new-state 7)
   (let $left (new-state $leaf)
     (let $right (new-state $leaf)
       (SharedDiamond (prime-need:observe-diamond $left $right)))))
!(let $left-leaf (new-state 7)
   (let $right-leaf (new-state 7)
     (let $left (new-state $left-leaf)
       (let $right (new-state $right-leaf)
         (DuplicatedDiamond (prime-need:observe-diamond $left $right))))))

; A private payload interprets both stored handles and query handles in its
; own Space view. Retrieval and literal matching recognize the same alias;
; an unrelated empty Space stays distinct. Stored duplicates remain rows.
!(let $cell (new-space)
   (let $store (new-space)
     (chain (add-atom $store $cell) $added
       (prime-need:run-payload
         (rho:eval-payload
           (quote (Pair (collapse (match $store $cell Hit))
                        (let $found (get-atoms $store) (== $found $cell)))){$store $cell})))))
!(let $cell (new-space)
   (let $other (new-space)
     (let $store (new-space)
       (chain (add-atom $store $cell) $added
         (prime-need:run-payload
           (rho:eval-payload
             (quote (Pair (collapse (match $store $other Hit))
                          (let $found (get-atoms $store) (== $found $other)))){$store $other}))))))
!(let $cell (new-space)
   (let $store (new-space)
     (chain (add-atom $store (StoredResource $cell $cell)) $first
       (chain (add-atom $store (StoredResource $cell $cell)) $second
         (prime-need:run-payload
           (rho:eval-payload
             (quote (collapse (match $store (StoredResource $cell $cell) Hit))){$store $cell}))))))

; Removing through that alias removes one actual private occurrence and
; leaves the caller's store intact. Nodup observes the same resource view.
!(let $cell (new-space)
   (let $store (new-space)
     (chain (add-atom $store $cell) $first
       (chain (add-atom $store $cell) $second
         (chain
           (prime-need:run-payload
             (rho:eval-payload
               (quote (chain (remove-atom $store $cell) $removed
                        (Pair (collapse (match $store $cell Hit)) (space-len $store)))){$store $cell}))
           $inside (Pair $inside (space-len $store)))))))
!(let $cell (new-space)
   (let $other (new-space)
     (let $store (new-space)
       (chain (add-atom $store $cell) $added
         (prime-need:run-payload
           (rho:eval-payload
             (quote (chain (add-atom-nodup $store $cell) $same
                      (chain (space-len $store) $one
                        (chain (add-atom-nodup $store $other) $different
                          (Pair $one (space-len $store)))))){$store $cell $other}))))))

; Local backend profiles preserve the same visible rows through private
; read and mutation views, including a removed base row and a new local row.
!(let $store (new-space)
   (chain (add-atom $store (Row 1)) $one
     (chain (add-atom $store (Row 2)) $two
       (prime-need:run-payload
         (rho:eval-payload (quote (collapse (get-atoms $store))){$store})))))
!(let $store (new-space)
   (chain (add-atom $store (Row 1)) $one
     (chain (add-atom $store (Row 2)) $two
       (chain (prime-need:run-payload
         (rho:eval-payload (quote (chain (add-atom $store (Row 3)) $three
           (chain (remove-atom $store (Row 1)) $gone
             (collapse (get-atoms $store))))){$store})) $inside
         (Pair $inside (collapse (get-atoms $store)))))))
