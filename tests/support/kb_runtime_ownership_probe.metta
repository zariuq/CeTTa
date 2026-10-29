!(import! &self (library store))
(= (kb-owner:encode $value) (store:encode $value))
(= (kb-owner:run (Committed $value))
   (let $installed (transaction (add-atom &self (= (kb-owner:committed $x) $x)))
     (let $function kb-owner:committed (store:encode ($function $value)))))
(= (kb-owner:run Install)
   (transaction (add-atom &self (= (kb-owner:persistent $x) (store:encode $x)))))
(= (kb-owner:run (NextRequest $value))
   (let $function kb-owner:persistent ($function $value)))
(= (kb-owner:run (OuterToInner $value))
   (transaction
     (let $installed (add-atom &self (= (kb-owner:outer $x) $x))
       (transaction (let $function kb-owner:outer (store:encode ($function $value)))))))
(= (kb-owner:run (InnerToOuter $value))
   (transaction
     (let $installed (transaction (add-atom &self (= (kb-owner:inner $x) $x)))
       (let $function kb-owner:inner (store:encode ($function $value))))))
(= (kb-owner:run (Parallel $value))
   (collapse (hyperpose ((kb-owner:encode $value) (kb-owner:encode $value)))))
(= (kb-owner:run (ParallelAdded $value))
   (collapse (hyperpose
     ((transaction
        (let $installed (add-atom &self (= (kb-owner:parallel-left $x) $x))
          (let $function kb-owner:parallel-left (store:encode ($function $value)))))
      (transaction
        (let $installed (add-atom &self (= (kb-owner:parallel-right $x) $x))
          (let $function kb-owner:parallel-right (store:encode ($function $value)))))))))
