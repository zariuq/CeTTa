; The absence of sources, (absent (, c1 ... cn)) or (absent (read R p)),
; is a predicate query: it holds when no row of its sources exists.  It
; reads every region its sources read, so it needs the observe facet at
; each of them, as (absent R p) does at R.  Without it the transform is not
; formed, whatever the transform's own region allows.
!(import! &self interaction)
!(bind! &plain (new-space))

; a channel's holder has no observe: neither form of absence is formed
!(let $c (new-region channel) (transform &plain (absent $c (msg $x)) empty))
!(let $c (new-region channel) (transform &plain (absent (read $c (msg $x))) empty))
; nor through a peek-only or a send-only handle
!(let* (($c (new-region channel)) ($p (facet $c (peek))))
   (transform &plain (absent (read $p (msg $x))) via-peek))
!(let* (($c (new-region channel)) ($s (facet $c (send))))
   (transform &plain (absent (read $s (msg $x))) via-send))
; one source without observe refuses the conjunction
!(let* (($l (new-region log)) ($c (new-region channel)))
   (transform &plain (absent (, (read $l (msg $x)) (read $c (ack $x)))) none))

; a log's holder observes: the absence holds while nothing matches
!(let $l (new-region log) (transform &plain (absent (read $l (msg $x))) none-logged))
!(let $l (new-region log)
   (transform &plain (absent (, (read $l (msg $x)) (read $l (ack $x)))) no-acked-message))
; and fails once a matching occurrence is there
!(let $l (new-region log)
   (let $_ (transform $l () (add-atom $l (msg 7)))
     (collapse (transform &plain (absent (read $l (msg $x))) none-logged))))
!(let $l (new-region log)
   (let $_ (transform $l () (, (add-atom $l (msg 7)) (add-atom $l (ack 7))))
     (collapse (transform &plain (absent (, (read $l (msg $x)) (read $l (ack $x)))) no-acked-message))))
; a plain space has every right
!(transform &plain (absent (, (read (msg $x)) (read (ack $x)))) plain-empty)
