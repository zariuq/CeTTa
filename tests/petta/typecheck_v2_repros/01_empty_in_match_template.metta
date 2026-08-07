(: BaseRateEntry Type)
(: base-rate-cache-remove-entry! (-[det]-> BaseRateEntry Bool))
(= (base-rate-cache-remove-entry! $entry) (remove-atom &base_rate_cache $entry))

(: foreach-flat (-[$e]-> (-[$e]-> $item $effect) (List $item) (List $none)))
(= (foreach-flat $f ()) ())
(= (foreach-flat $f (cons $x $xs)) (progn ($f $x) (foreach-flat $f $xs)))

(: KB (Newtype Atom))
(: invalidate-computed-base-rates! (-[det]-> KB (List $none)))
(= (invalidate-computed-base-rates! $kbid)
   (let*
      (($entries
          (collapse
             (match &base_rate_cache
                (base-rate $kbid $stored $tv $source $mode)
                (if (== $source user) (empty)
                   (base-rate $kbid $stored $tv $source $mode)))))
       ($_ (foreach-flat base-rate-cache-remove-entry! $entries)))
      ()))
!(println! ok)
