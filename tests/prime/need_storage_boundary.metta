; Persistent spaces store ordinary Prime syntax, never evaluator-private
; thunk references, and crossing the boundary is reification rather than
; forcing.
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
; Storage reifies the source without running the pending effect. Ordinary
; return subsequently observes its value. A declared Data view retrieves
; the stored source without executing it; neither view exposes heap IDs.
!(match &prime-need:stored (Stored $saved) $saved)
(: prime-need:stored-code (-> (Data Atom) (Data Atom)))
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

; One matched compound value keeps aliases throughout substitution. Its
; ordinary return is data; explicit activation recovers the stored demand.
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
