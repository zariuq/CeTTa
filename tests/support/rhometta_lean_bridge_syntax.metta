!(import! &self rhometta)

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

(= (pure:comm)
   (rho:par
     (rho:send (rho:quote rho:nil) rho:nil)
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:data)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (+ 1 2)))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:multi)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (superpose (1 2))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:zero)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (superpose ())))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

!(let $sub (new-space)
   (let $_ (add-atom $sub original)
     (let $_ (add-atom &self (holder-space-direct $sub))
       (bind! &held-space-direct $sub))))

!(let $st (new-state 0)
   (let $_ (add-atom &self (holder-state-direct $st))
     (bind! &held-state-direct $st)))

!(let $sub (new-space)
   (let $_ (add-atom $sub original)
     (let $_ (add-atom &self (holder-space-sibling $sub))
       (bind! &held-space-sibling $sub))))

!(add-atom &self (foo 1))
!(add-atom &self (foo 2))
!(add-atom &self (foo 3))

(= (eval:kb)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval (match &self (foo $x) (got $x))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (unwrap-rho-val (rho:val $x)) $x)

(= (eval:state-local)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (new-state 0)
           (let $_ (change-state! $s 1)
             (get-state $s)))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:owned-space)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-space-direct $x) $x)
           (let $_ (add-atom $s injected)
             $s))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (bridge:owned-space-ok)
   (let $owned (unwrap-rho-val (rhometta:run (eval:owned-space)))
     (let $_ (add-atom $owned postrun)
       (if (== (collapse (get-atoms $owned)) (original injected postrun))
           (== (collapse (get-atoms &held-space-direct)) (original))
           False))))

(= (eval:owned-state)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-state-direct $x) $x)
           (let $_ (change-state! $s 1)
             $s))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (bridge:owned-state-ok)
   (let $owned (unwrap-rho-val (rhometta:run (eval:owned-state)))
     (let $_ (change-state! $owned 2)
       (if (== (get-state $owned) 2)
           (== (get-state &held-state-direct) 0)
           False))))

(= (eval:owned-space-aliased)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-space-direct $x) $x)
           (let $_ (add-atom $s injected)
             (superpose ($s (owned-space-wrap $s)))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (bridge:owned-space-aliased-fault)
   (let $_ (assertEqualToResult
             (let $got (collapse (rhometta:run (eval:owned-space-aliased)))
               (car-atom $got))
             ((Error rhometta:eval PayloadOwnedExportAliased)))
     True))

(= (eval:owned-state-aliased)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-state-direct $x) $x)
           (let $_ (change-state! $s 1)
             (superpose ($s (owned-state-wrap $s)))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (bridge:owned-state-aliased-fault)
   (let $_ (assertEqualToResult
             (let $got (collapse (rhometta:run (eval:owned-state-aliased)))
               (car-atom $got))
             ((Error rhometta:eval PayloadOwnedExportAliased)))
     True))

(= (eval:sibling-pair)
   (rho:par
     (rho:send (chan 0)
       (rhometta:eval
         (let $s (match &self (holder-space-sibling $x) $x)
           (let $_ (add-atom $s injected)
             done))))
     (rho:recv (chan 0) $x rho:nil)
     (rho:send (chan 1)
       (rhometta:eval
         (let $s (match &self (holder-space-sibling $x) $x)
           (reader-space (get-atoms $s)))))
     (rho:recv (chan 1) $y (rho:drop $y))))

(= (bridge:sibling-pair-ok)
   (if (== (collapse (rhometta:run (eval:sibling-pair)))
           ((rho:val (reader-space original))))
       (== (collapse (get-atoms &held-space-sibling)) (original))
       False))

(= (eval:contended-pair)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (+ 1 2)))
     (rho:recv (rho:quote rho:nil) $x (rho:val left))
     (rho:recv (rho:quote rho:nil) $x (rho:val right))))

(= (eval:chained-two-round)
   (rho:par
     (rho:send (chan 0) (rhometta:eval (+ 1 2)))
     (rho:recv (chan 0) $x (rho:send (chan 1) rho:nil))
     (rho:recv (chan 1) $y (rho:val chained))
     (rho:send (chan 2) (rhometta:eval (+ 2 3)))
     (rho:recv (chan 2) $z (rho:drop $z))))
