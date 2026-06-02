; Query-surface companions for provenance-sensitive PLN use.
; These helpers do not replace PLN.Query. They expose:
;   - full matching candidate audits,
;   - all top-confidence ties,
;   - lightweight pruning for same-strength weaker candidates.

!(import! &self lib_wmpln)

(= (PLN.CandidateStrength ((stv $f $c) $Ev))
   $f)

(= (PLN.CandidateConfidence ((stv $f $c) $Ev))
   $c)

(= (PLN.TupleContains $tuple $item)
   (not (== (collapse (let $x (superpose $tuple)
                          (if (== $x $item)
                              True
                              (empty))))
            ())))

(= (PLN.QueryAuditFromBeliefs $BeliefsRet $term)
   (collapse
     (case (superpose $BeliefsRet)
       (((Sentence ($Term $TV) $Ev)
         (case (== $Term $term)
           ((True ($TV $Ev)))))))))

(= (PLN.QueryAudit $Tasks $Beliefs $term $maxsteps $taskqueuesize $beliefqueuesize)
   (let ($TasksRet $BeliefsRet)
        (PLN.Derive $Tasks $Beliefs $maxsteps $taskqueuesize $beliefqueuesize)
        (PLN.QueryAuditFromBeliefs $BeliefsRet $term)))

(= (PLN.QueryAudit $kb $term $maxsteps $taskqueuesize $beliefqueuesize)
   (PLN.QueryAudit $kb $kb $term $maxsteps $taskqueuesize $beliefqueuesize))

(= (PLN.QueryAudit $kb $term $maxsteps)
   (PLN.QueryAudit $kb $term $maxsteps (PLN.Config.TaskQueueSize) (PLN.Config.BeliefQueueSize)))

(= (PLN.QueryAudit $kb $term)
   (PLN.QueryAudit $kb $term (PLN.Config.MaxSteps)))

(= (PLN.HasStrongerConfidence $audit $cand)
   (not (== (collapse
              (let $other (superpose $audit)
                   (if (> (PLN.CandidateConfidence $other)
                          (PLN.CandidateConfidence $cand))
                       True
                       (empty))))
            ())))

(= (PLN.QueryTopTiesFromAudit $audit)
   (if (== $audit ())
       ()
       (collapse
         (let $cand (superpose $audit)
              (if (PLN.HasStrongerConfidence $audit $cand)
                  (empty)
                  $cand)))))

(= (PLN.QueryTopTies $Tasks $Beliefs $term $maxsteps $taskqueuesize $beliefqueuesize)
   (PLN.QueryTopTiesFromAudit
     (PLN.QueryAudit $Tasks $Beliefs $term $maxsteps $taskqueuesize $beliefqueuesize)))

(= (PLN.QueryTopTies $kb $term $maxsteps $taskqueuesize $beliefqueuesize)
   (PLN.QueryTopTies $kb $kb $term $maxsteps $taskqueuesize $beliefqueuesize))

(= (PLN.QueryTopTies $kb $term $maxsteps)
   (PLN.QueryTopTies $kb $term $maxsteps (PLN.Config.TaskQueueSize) (PLN.Config.BeliefQueueSize)))

(= (PLN.QueryTopTies $kb $term)
   (PLN.QueryTopTies $kb $term (PLN.Config.MaxSteps)))

(= (PLN.WeakerSameValue $other $cand)
   (and (== (PLN.CandidateStrength $other)
            (PLN.CandidateStrength $cand))
        (> (PLN.CandidateConfidence $other)
           (PLN.CandidateConfidence $cand))))

(= (PLN.HasStrongerSameValue $audit $cand)
   (not (== (collapse
              (let $other (superpose $audit)
                   (if (PLN.WeakerSameValue $other $cand)
                       True
                       (empty))))
            ())))

(= (PLN.QueryPrunedAuditFromAudit $audit)
   (if (== $audit ())
       ()
       (collapse
         (let $cand (superpose $audit)
              (if (PLN.HasStrongerSameValue $audit $cand)
                  (empty)
                  $cand)))))

(= (PLN.QueryPrunedAudit $Tasks $Beliefs $term $maxsteps $taskqueuesize $beliefqueuesize)
   (PLN.QueryPrunedAuditFromAudit
     (PLN.QueryAudit $Tasks $Beliefs $term $maxsteps $taskqueuesize $beliefqueuesize)))

(= (PLN.QueryPrunedAudit $kb $term $maxsteps $taskqueuesize $beliefqueuesize)
   (PLN.QueryPrunedAudit $kb $kb $term $maxsteps $taskqueuesize $beliefqueuesize))

(= (PLN.QueryPrunedAudit $kb $term $maxsteps)
   (PLN.QueryPrunedAudit $kb $term $maxsteps (PLN.Config.TaskQueueSize) (PLN.Config.BeliefQueueSize)))

(= (PLN.QueryPrunedAudit $kb $term)
   (PLN.QueryPrunedAudit $kb $term (PLN.Config.MaxSteps)))
