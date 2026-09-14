(= (call-snapshot-probe)
   (let $_ (remove-atom &self (= (call-snapshot-probe) second))
      first))
(= (call-snapshot-probe) second)

!(msort
   (collapse
      (hyperpose
         ((collapse (call-snapshot-probe))
          companion))))

!(collapse (call-snapshot-probe))
