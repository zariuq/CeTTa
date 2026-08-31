; ===========================================================================
; rho-reflective-server -- a persistent server, with replication DERIVED from
; reflection (there is no replication primitive in sight).
;
; Three clients each hand the server a private reply-channel; the server answers
; every one of them.  The catch: a plain receive fires ONCE, so on its own it
; could serve only the first client.  To keep serving, the server must re-create
; its own listener after each answer.  In the pure rho-calculus there is no "!"
; (replication) operator to do that -- so rho:patterns:!recv builds the persistent
; listener out of REFLECTION alone (quote/drop), via Meredith & Radestock's
; encoding  D(x) = x(y).( x[y] | *y ).  That is the calculus's signature result:
; replication (and recursion) fall out of reflection.  "A server that keeps
; running" is DERIVED, not a primitive.
;
; Every reaction here is a pure rho COMM -- reflection (quote/drop) only, with NO
; eval-at-COMM payload (contrast the rhometta demos, where the payload is a MeTTa
; term run at COMM). This is NOT a standalone --lang rhocalc file, though: it imports
; the rho:patterns macro for the replicated input and uses rhometta:run as the
; reducer plus a small projection, so the output is a readable count of clients
; served rather than a raw residual.
;
; Run:  ./cetta --quiet --profile extended --lang he <this file>
; ===========================================================================

!(import! &self rhometta)
!(import! &self rho:patterns)

; Standard rho notation used below:
;   P | Q                 parallel composition
;   x!(m)                send process m on channel x
;   for (y <- x) { P }  receive from x, bind y in P
;   *y                   drop: run the quoted process/name y
;   @P                   quote process P as a name

; A channel is just a name; mint distinct ones by number.
(= (chan $n) (if (== $n 0) (rho:quote rho:nil) (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; The persistent server on (chan 1): for each reply-channel it receives, it
; answers there -- and re-arms itself.  rho:patterns:!recv is the reflective
; replicated input; the re-arming is what makes it persistent.
; Standard reading:
;   server  ~=  ! for (reply <- 1) { reply!(0) }
; where the "!" is not primitive: it is encoded from quote/drop.
(= (server)
   (rho:patterns:!recv (chan 2) (chan 1) $reply (rho:send $reply rho:nil)))

; A client: hand the server my private reply-channel, await the answer, record
; that I was served.
; Standard reading:
;   client(r)  ~=  1[r] | for (ans <- r) { served-one }
; Here 1[r] is the usual sugar for "send the name r on channel 1".
(= (client $reply)
   (rho:par (rho:send (chan 1) (rho:drop (chan $reply)))
            (rho:recv (chan $reply) $ans (rho:val served-one))))

(= (net) (rho:par (server) (client 3) (client 4) (client 5)))

; --- readable output: count how many clients were served --------------------
(= (count-tuple $t) (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))
(= (served-of $r)
   (case $r
     (((rho:val served-one) served-one)
      ((rho:par $a $b) (superpose ((served-of $a) (served-of $b))))
      ((rho:par $a $b $c) (superpose ((served-of $a) (served-of $b) (served-of $c))))
      ((rho:par $a $b $c $d) (superpose ((served-of $a) (served-of $b) (served-of $c) (served-of $d))))
      ((rho:par $a $b $c $d $e) (superpose ((served-of $a) (served-of $b) (served-of $c) (served-of $d) (served-of $e))))
      ((rho:par $a $b $c $d $e $f) (superpose ((served-of $a) (served-of $b) (served-of $c) (served-of $d) (served-of $e) (served-of $f))))
      ($_ (empty)))))

(= (verdict $e $g) (if (== $e $g) PASS *FAIL*))
(= (gate $tag $e $g $d) (=== $tag (verdict $e $g) expect $e got $g : $d))

; ===========================================================================
!(gate ACT1-all-served 3 (count-tuple (collapse (served-of (rhometta:run (net)))))
   "the reflective server re-creates its listener after each answer => all 3 clients served")

!("clients served by the persistent reflective server (regression-checked):")
!(collapse (served-of (rhometta:run (net))))
