; Additional tail-safe eval GC adversarial witnesses.
;
; This file targets smaller seams that are easy to miss in a broad corpus:
; indirect variable capture through a constructed match pattern, state cells
; retaining structured payloads across forced collections, table replay mixed
; with state publication, and stream calls whose policy/limit argument must be
; evaluated before dispatch.

(= (gc-wide-payload $n)
   (payload $n
     p00 p01 p02 p03 p04 p05 p06 p07
     p08 p09 p10 p11 p12 p13 p14 p15
     p16 p17 p18 p19 p20 p21 p22 p23
     p24 p25 p26 p27 p28 p29 p30 p31
     p32 p33 p34 p35 p36 p37 p38 p39
     p40 p41 p42 p43 p44 p45 p46 p47
     p48 p49 p50 p51 p52 p53 p54 p55
     p56 p57 p58 p59 p60 p61 p62 p63))

(= (gc-burn-to $n $then)
   (if (== $n 0)
       $then
       (let $dead (gc-wide-payload $n)
         (gc-burn-to (- $n 1) $then))))

(gc-capture-row left alpha)
(gc-capture-row right beta)

(= (gc-indirect-capture $n)
   (gc-burn-to $n
     (let $pack ((gc-capture-row $k $v) (captured $k $v))
       (let ($pattern $rest) (decons-atom $pack)
         (let ($result) $rest
           (match &self $pattern $result))))))

!(assertEqualToResult
  (gc-indirect-capture 96)
  ((captured left alpha) (captured right beta)))

!(bind! &gc-state-payload
  (new-state (state-payload (gc-wide-payload 0))))

(= (gc-state-payload-loop $n)
   (if (== $n 0)
       (get-state &gc-state-payload)
       (let $dead (gc-wide-payload $n)
         (let $_ (change-state! &gc-state-payload (state-payload $dead))
           (gc-state-payload-loop (- $n 1))))))

!(assertEqual
  (gc-state-payload-loop 64)
  (state-payload (gc-wide-payload 1)))

(= (gc-stream-limit-loop $n)
   (gc-burn-to $n
     (select (+ 1 1) (superpose (stream-left stream-right stream-extra)))))

!(assertEqual
  (gc-stream-limit-loop 96)
  (stream-left stream-right))

!(bind! &gc-stage-state (new-state (stage-state initial)))

(= (gc-stage-source key)
   (let $_ (change-state! &gc-stage-state (stage-state key))
     (stage-value key (get-state &gc-stage-state))))

(= (gc-stage-loop $n)
   (if (== $n 0)
       (gc-stage-source key)
       (let $dead (gc-wide-payload $n)
         (gc-stage-loop (- $n 1)))))

!(let* (
    (() (pragma! search-table-mode variant))
    (() (assertEqualToResult
          (gc-stage-loop 96)
          ((stage-value key (stage-state key)))))
    (() (assertEqual
          (get-state &gc-stage-state)
          (stage-state key)))
    (() (assertEqualToResult
          (gc-stage-loop 96)
          ((stage-value key (stage-state key)))))
  )
  ())
