; An argument passed on lazily is still the program the caller wrote when it
; reaches a process position: a wrapper around par keeps its processes'
; steps interleaved, as par itself does.
!(import! &self interaction)
!(par (seq (println! a1) $u (println! a2)) (seq (println! b1) $w (println! b2)))
(= (lw:par $a $b) (par $a $b))
!(lw:par (seq (println! c1) $u (println! c2)) (seq (println! d1) $w (println! d2)))
