; An ascription in one branch types the value in that branch only; the
; other branch returns an arbitrary atom where a Score is promised.
(: Score Type)
(: zero-score Score)
(: score-of (-[det]-> Atom Score))
(= (score-of $value)
   (if (is-var $value)
      (the Score $value)
      $value))
