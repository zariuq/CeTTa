; A call that no declared overload admits raises no_matching_overload.
(: scale (-> Number Number))
(: scale (-> String String))
(= (scale $x) $x)

!(test (let* (($call (quote (scale True)))
              ($caught (catch (eval $call))))
         (repr $caught))
       "(Error (no_matching_overload scale) typecheck)")
