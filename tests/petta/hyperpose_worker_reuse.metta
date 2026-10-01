; With one worker, jobs reuse that worker's private state.
!(callPredicate (Predicate (set_prolog_flag cpu_count 1)))
!(change-state! reused 10)
!(collapse (hyperpose ((change-state! reused 42) (get-state reused))))
!(get-state reused)
!(collapse (hyperpose ((car-atom (catch (get-state reused))))))
