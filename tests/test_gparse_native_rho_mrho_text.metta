!(import! &self gparse)

!(assertEqual
  (gparse:rho-canon->mrho-text (rho:send (rho:quote rho:nil) rho:nil))
  (Ok "(rho:send (rho:quote rho:nil) rho:nil)"))

!(assertEqual
  (gparse:rho-canon->mrho-text
    (rho:recv-db (rho:quote rho:nil) (rho:drop (rho:bound 0))))
  (Ok "(rho:recv (rho:quote rho:nil) $x (rho:drop $x))"))

!(assertEqual
  (gparse:rho-canon->mrho-text
    (rho:par
      (rho:send (rho:quote rho:nil) rho:nil)
      (rho:recv-db (rho:quote rho:nil) (rho:drop (rho:bound 0)))))
  (Ok "(rho:par (rho:send (rho:quote rho:nil) rho:nil) (rho:recv (rho:quote rho:nil) $x (rho:drop $x)))"))

!(assertEqual
  (gparse:rho-canon->mrho-text
    (rho:par
      (rho:send (rho:quote rho:nil) (rho:drop (rho:quote rho:nil)))
      (rho:recv-db (rho:quote rho:nil) (rho:drop (rho:bound 0)))))
  (Ok "(rho:par (rho:send (rho:quote rho:nil) (rho:drop (rho:quote rho:nil))) (rho:recv (rho:quote rho:nil) $x (rho:drop $x)))"))

!(assertEqual
  (gparse:rho-canon->mrho-text
    (rho:recv-db
      (rho:quote rho:nil)
      (rho:recv-db (rho:bound 0) (rho:drop (rho:bound 1)))))
  (Ok "(rho:recv (rho:quote rho:nil) $x (rho:recv $x $y (rho:drop $x)))"))

!(assertEqual
  (case (gparse:rho-canon->mrho-text (rho:send (rho:quote rho:nil) rho:nil))
    (((Ok $s) $s)
     ($o $o)))
  "(rho:send (rho:quote rho:nil) rho:nil)")

!(assertEqual
  (gparse:rho-canon->mrho-text (rho:drop (rho:bound 0)))
  (Err "rho:bound index out of scope"))
