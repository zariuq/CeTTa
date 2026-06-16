!(import! &self rhometta)

!(let $sub (new-space)
   (let $_ (add-atom $sub original)
     (let $_ (add-atom &self (holder-space-direct $sub))
       (bind! &held-space-direct $sub))))

!(let $st (new-state 0)
   (let $_ (add-atom &self (holder-state-direct $st))
     (bind! &held-state-direct $st)))

!(let $sub (new-space)
   (let $_ (add-atom $sub original)
     (let $_ (add-atom &self (holder-space-wrapped $sub))
       (bind! &held-space-wrapped $sub))))

!(let $st (new-state 0)
   (let $_ (add-atom &self (holder-state-wrapped $st))
     (bind! &held-state-wrapped $st)))

!(let $sub (new-space)
   (let $_ (add-atom $sub original)
     (let $_ (add-atom &self (holder-space-sibling $sub))
       (bind! &held-space-sibling $sub))))

!(let $st (new-state 0)
   (let $_ (add-atom &self (holder-state-sibling $st))
     (bind! &held-state-sibling $st)))

(= (lookup-space-wrapped)
   (let $s (match &self (holder-space-wrapped $x) $x) $s))

(= (lookup-state-wrapped)
   (let $s (match &self (holder-state-wrapped $x) $x) $s))

(= (unwrap-rho-val (rho:val $x)) $x)

(= (eval:ground-space-direct)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-space-direct $x) $x)
           (let $_ (add-atom $s injected)
             (direct-space (get-atoms $s))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:ground-state-direct)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-state-direct $x) $x)
           (let $_ (change-state! $s 1)
             (direct-state (get-state $s))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:ground-space-wrapped)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (lookup-space-wrapped)
           (let $_ (add-atom $s injected)
             (wrapped-space (get-atoms $s))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:ground-state-wrapped)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (lookup-state-wrapped)
           (let $_ (change-state! $s 1)
             (wrapped-state (get-state $s))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:ground-space-owned)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-space-direct $x) $x)
           (let $_ (add-atom $s injected)
             $s))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:ground-state-owned)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-state-direct $x) $x)
           (let $_ (change-state! $s 1)
             $s))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:ground-space-owned-aliased)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-space-direct $x) $x)
           (let $_ (add-atom $s injected)
             (superpose ($s (owned-space-wrap $s)))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:ground-state-owned-aliased)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval
         (let $s (match &self (holder-state-direct $x) $x)
           (let $_ (change-state! $s 1)
             (superpose ($s (owned-state-wrap $s)))))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (ch:writer) (rho:quote rho:nil))
(= (ch:reader) (rho:quote (rho:send (rho:quote rho:nil) rho:nil)))

(= (eval:ground-space-sibling)
   (rho:par
     (rho:send (ch:writer)
       (rhometta:eval
         (let $s (match &self (holder-space-sibling $x) $x)
           (let $_ (add-atom $s injected)
             (writer-space (get-atoms $s))))))
     (rho:recv (ch:writer) $x (rho:drop $x))
     (rho:send (ch:reader)
       (rhometta:eval
         (let $s (match &self (holder-space-sibling $x) $x)
           (reader-space (get-atoms $s)))))
     (rho:recv (ch:reader) $y (rho:drop $y))))

(= (eval:ground-state-sibling)
   (rho:par
     (rho:send (ch:writer)
       (rhometta:eval
         (let $s (match &self (holder-state-sibling $x) $x)
           (let $_ (change-state! $s 1)
             (writer-state (get-state $s))))))
     (rho:recv (ch:writer) $x (rho:drop $x))
     (rho:send (ch:reader)
       (rhometta:eval
         (let $s (match &self (holder-state-sibling $x) $x)
           (reader-state (get-state $s)))))
     (rho:recv (ch:reader) $y (rho:drop $y))))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-space-direct)))
  (((rho:val (direct-space injected)) (rho:val (direct-space original)))))

!(assertEqualToResult
  (collapse (get-atoms &held-space-direct))
  ((original)))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-state-direct)))
  (((rho:val (direct-state 1)))))

!(assertEqualToResult
  (get-state &held-state-direct)
  (0))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-space-wrapped)))
  (((rho:val (wrapped-space injected)) (rho:val (wrapped-space original)))))

!(assertEqualToResult
  (collapse (get-atoms &held-space-wrapped))
  ((original)))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-state-wrapped)))
  (((rho:val (wrapped-state 1)))))

!(assertEqualToResult
  (get-state &held-state-wrapped)
  (0))

!(bind! &owned-space-result
   (unwrap-rho-val (rhometta:run (eval:ground-space-owned))))

!(assertEqualToResult
  (collapse (get-atoms &owned-space-result))
  ((original injected)))

!(add-atom &owned-space-result postrun)

!(assertEqualToResult
  (collapse (get-atoms &owned-space-result))
  ((original injected postrun)))

!(assertEqualToResult
  (collapse (get-atoms &held-space-direct))
  ((original)))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-space-owned-aliased)))
  (((Error rhometta:eval PayloadOwnedExportAliased))))

!(bind! &owned-state-result
   (unwrap-rho-val (rhometta:run (eval:ground-state-owned))))

!(assertEqualToResult
  (get-state &owned-state-result)
  (1))

!(let $_ (change-state! &owned-state-result 2) ())

!(assertEqualToResult
  (get-state &owned-state-result)
  (2))

!(assertEqualToResult
  (get-state &held-state-direct)
  (0))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-state-owned-aliased)))
  (((Error rhometta:eval PayloadOwnedExportAliased))))

!(assertAlphaEqualToResult
  (collapse (rhometta:run (eval:ground-space-sibling)))
  (((rho:par (rho:val (reader-space original)) (rho:val (writer-space injected)))
    (rho:par (rho:val (reader-space original)) (rho:val (writer-space original))))))

!(assertEqualToResult
  (collapse (get-atoms &held-space-sibling))
  ((original)))

!(assertAlphaEqualToResult
  (collapse (rhometta:run (eval:ground-state-sibling)))
  (((rho:par (rho:val (reader-state 0)) (rho:val (writer-state 1))))))

!(assertEqualToResult
  (get-state &held-state-sibling)
  (0))

!(pragma! num-threads 2)

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-space-direct)))
  (((rho:val (direct-space injected)) (rho:val (direct-space original)))))

!(assertEqualToResult
  (collapse (get-atoms &held-space-direct))
  ((original)))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-state-direct)))
  (((rho:val (direct-state 1)))))

!(assertEqualToResult
  (get-state &held-state-direct)
  (0))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-space-wrapped)))
  (((rho:val (wrapped-space injected)) (rho:val (wrapped-space original)))))

!(assertEqualToResult
  (collapse (get-atoms &held-space-wrapped))
  ((original)))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-state-wrapped)))
  (((rho:val (wrapped-state 1)))))

!(assertEqualToResult
  (get-state &held-state-wrapped)
  (0))

!(bind! &owned-space-result-threaded
   (unwrap-rho-val (rhometta:run (eval:ground-space-owned))))

!(assertEqualToResult
  (collapse (get-atoms &owned-space-result-threaded))
  ((original injected)))

!(add-atom &owned-space-result-threaded threaded-postrun)

!(assertEqualToResult
  (collapse (get-atoms &owned-space-result-threaded))
  ((original injected threaded-postrun)))

!(assertEqualToResult
  (collapse (get-atoms &held-space-direct))
  ((original)))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-space-owned-aliased)))
  (((Error rhometta:eval PayloadOwnedExportAliased))))

!(bind! &owned-state-result-threaded
   (unwrap-rho-val (rhometta:run (eval:ground-state-owned))))

!(assertEqualToResult
  (get-state &owned-state-result-threaded)
  (1))

!(let $_ (change-state! &owned-state-result-threaded 3) ())

!(assertEqualToResult
  (get-state &owned-state-result-threaded)
  (3))

!(assertEqualToResult
  (get-state &held-state-direct)
  (0))

!(assertEqualToResult
  (collapse (rhometta:run (eval:ground-state-owned-aliased)))
  (((Error rhometta:eval PayloadOwnedExportAliased))))

!(assertAlphaEqualToResult
  (collapse (rhometta:run (eval:ground-space-sibling)))
  (((rho:par (rho:val (reader-space original)) (rho:val (writer-space injected)))
    (rho:par (rho:val (reader-space original)) (rho:val (writer-space original))))))

!(assertEqualToResult
  (collapse (get-atoms &held-space-sibling))
  ((original)))

!(assertAlphaEqualToResult
  (collapse (rhometta:run (eval:ground-state-sibling)))
  (((rho:par (rho:val (reader-state 0)) (rho:val (writer-state 1))))))

!(assertEqualToResult
  (get-state &held-state-sibling)
  (0))
