; ============================================================================
; IDENTIFIED EVIDENCE LEDGER FOR RANDOM CLOSURE RESPONSES (--lang petta)
; ============================================================================
;
; Connects named, channel-tagged observations (hard choice assertions, soft
; likelihood ratios, isolated local component symptom probes, and quantized
; interior-support affine-sigmoid neural channels) to lib_random_closure.
;
; Key semantic & independence contracts:
;   1. Immutable observation-ID catalogue: every registered obs-id permanently
;      denotes its initial observation payload (aligned with the fixed
;      observation catalogue in DistributionalEvidence.lean). Replaying an
;      active obs-id with identical payload returns Replayed; re-observing a
;      retracted obs-id with identical payload (or via ledger:observe-id)
;      re-activates it; reusing any registered obs-id with a different payload
;      is rejected even after retraction.
;   2. Explicit sensor-channel independence: soft/noisy observations carry an
;      explicit channel-id. Distinct active noisy observations sharing the same
;      single-draw channel-id are rejected rather than silently multiplied as
;      independent confirmations. Convenience constructors (ledger:likelihood-obs,
;      ledger:local-symptom-obs, ledger:neural-bernoulli-obs) declare obs-id as
;      its own independent sensor draw; persistent correlated channels should be
;      modeled as shared or local random choices in RC:Program.
;   3. Interior neural likelihood support: ledger:affine-sigmoid-prob quantizes
;      the computed floating sigmoid onto the interior rational grid
;      [10^-12, 1 - 10^-12] subset (0, 1), rejecting NaN/Inf. This specifies
;      a quantized sensor model, not an end-to-end accuracy bound for an ideal
;      real affine-sigmoid calculation. Finite neural logits therefore
;      never fabricate hard zero likelihoods (RC:ImpossibleEvidence); exact
;      rational inference certifies posteriors for this quantized interior-support
;      sensor model.
;   4. Unnormalized local evidence & retraction recovery: local weights stay
;      unnormalized until root shared-cause mixing and query readout, and
;      retraction recomputes from the retained prior and remaining active
;      observations even after contradictory (Z = 0) states.
;
; Complexity note:
;   - Ledger catalogue / active-observation scans are O(|C| + |E|) in the total
;     number of declared choices |C| and registered/active observations |E|,
;     then dispatch a single-owner O(log N) update into lib_random_closure.
; ============================================================================

!(import! &self (library lib_random_closure))
!(import! &self (library tensor))
!(import! &self (library math))

; ----------------------------------------------------------------------------
; 1. Observation Constructors (Hard, Channel-Explicit Soft, & Neural)
; ----------------------------------------------------------------------------

(= (ledger:hard-obs $obs-id $choice $polarity)
   (if (or (not (rc:is-symbol $obs-id)) (not (rc:is-symbol $choice)))
       (RC:Unsupported non-symbol-observation-header ($obs-id $choice))
       (if (== $polarity true)
           (Ledger:Obs $obs-id Ledger:HardDeterministic $choice (Rat 0 1) (Rat 1 1))
           (if (== $polarity false)
               (Ledger:Obs $obs-id Ledger:HardDeterministic $choice (Rat 1 1) (Rat 0 1))
               (RC:Unsupported invalid-hard-observation-polarity $polarity)))))

(= (ledger:channel-likelihood-obs $obs-id $channel-id $choice $lik-absent $lik-present)
   (if (or (not (rc:is-symbol $obs-id))
           (or (not (rc:is-symbol $channel-id)) (not (rc:is-symbol $choice))))
       (RC:Unsupported non-symbol-observation-header ($obs-id $channel-id $choice))
       (if (== $channel-id Ledger:HardDeterministic)
           (RC:Unsupported reserved-channel-identifier $channel-id)
           (if (or (not (rc:rat-nonneg-valid? $lik-absent))
                   (not (rc:rat-nonneg-valid? $lik-present)))
               (RC:Unsupported invalid-observation-likelihoods ($lik-absent $lik-present))
               (Ledger:Obs
                 $obs-id $channel-id $choice
                 (let (Rat $na $da) $lik-absent  (rc:rat $na $da))
                 (let (Rat $np $dp) $lik-present (rc:rat $np $dp)))))))

; Convenience constructor declaring $obs-id as an independent sensor draw
(= (ledger:likelihood-obs $obs-id $choice $lik-absent $lik-present)
   (ledger:channel-likelihood-obs $obs-id $obs-id $choice $lik-absent $lik-present))

; Isolated local component symptom probe (explicit channel):
; Observes a component's private symptom atom on empty interface seed
; (depending only on that component's local choices and shared causes, so
; components remain conditionally independent given the shared causes).
(= (ledger:channel-symptom-obs $obs-id $channel-id $comp-name $private-atom $lik-absent $lik-present)
   (if (or (not (rc:is-symbol $obs-id))
           (or (not (rc:is-symbol $channel-id))
               (or (not (rc:is-symbol $comp-name)) (not (rc:is-symbol $private-atom)))))
       (RC:Unsupported non-symbol-symptom-header ($obs-id $channel-id $comp-name $private-atom))
       (if (== $channel-id Ledger:HardDeterministic)
           (RC:Unsupported reserved-channel-identifier $channel-id)
           (if (or (not (rc:rat-nonneg-valid? $lik-absent))
                   (not (rc:rat-nonneg-valid? $lik-present)))
               (RC:Unsupported invalid-symptom-likelihoods ($lik-absent $lik-present))
               (Ledger:SymptomObs
                 $obs-id $channel-id $comp-name $private-atom
                 (let (Rat $na $da) $lik-absent  (rc:rat $na $da))
                 (let (Rat $np $dp) $lik-present (rc:rat $np $dp)))))))

; Convenience constructor declaring $obs-id as an independent symptom probe draw
(= (ledger:local-symptom-obs $obs-id $comp-name $private-atom $lik-absent $lik-present)
   (ledger:channel-symptom-obs $obs-id $obs-id $comp-name $private-atom $lik-absent $lik-present))

(= (ledger:hard-symptom-obs $obs-id $comp-name $private-atom $polarity)
   (if (or (not (rc:is-symbol $obs-id))
           (or (not (rc:is-symbol $comp-name)) (not (rc:is-symbol $private-atom))))
       (RC:Unsupported non-symbol-symptom-header ($obs-id $comp-name $private-atom))
       (if (== $polarity true)
           (Ledger:SymptomObs
             $obs-id Ledger:HardDeterministic $comp-name $private-atom (Rat 0 1) (Rat 1 1))
           (if (== $polarity false)
               (Ledger:SymptomObs
                 $obs-id Ledger:HardDeterministic $comp-name $private-atom (Rat 1 1) (Rat 0 1))
               (RC:Unsupported invalid-hard-symptom-polarity $polarity)))))

; Quantized interior-support affine-sigmoid Bernoulli sensor model:
; Computes logit z = (W * x)_0 + bias via packed f64 tensors and math:exp,
; rejecting non-finite inputs and quantizing onto [10^-12, 1 - 10^-12] subset (0, 1)
; so every finite logit preserves strict full Bernoulli support (0 < p < 1).
; Cancellation in matvec or bias addition and error in the floating sigmoid
; remain part of this computed model. Certified real-input likelihoods require
; propagation of bounds through all three operations.
(= (ledger:affine-sigmoid-prob $weights-mat $features-vec $bias)
   (if (!= (get-type $bias) Number)
       (RC:Unsupported non-numeric-neural-bias $bias)
       (let $mv (tensor:matvec $weights-mat $features-vec)
         (case (tensor:at $mv 0)
           (((tensor:present $dot)
             (if (or (isnan-math $dot)
                     (or (isinf-math $dot)
                         (or (isnan-math $bias) (isinf-math $bias))))
                 (RC:Unsupported non-finite-neural-logit ($dot $bias))
                 (let $z (+ $dot $bias)
                   (if (or (isnan-math $z) (isinf-math $z))
                       (RC:Unsupported non-finite-neural-logit $z)
                       (let* (($clamped (math:clamp -40.0 40.0 $z))
                              ($p (/ 1.0 (+ 1.0 (math:exp (- 0.0 $clamped)))))
                              ($scale 1000000000000)
                              ($raw-num (round-math (* $p (+ 0.0 $scale))))
                              ($num (if (< $raw-num 1)
                                        1
                                        (if (>= $raw-num $scale) (- $scale 1) $raw-num))))
                         (rc:rat $num $scale))))))
            ($other
             (RC:Unsupported invalid-neural-tensor-inputs ($weights-mat $features-vec))))))))

(= (ledger:channel-neural-bernoulli-obs
     $obs-id $channel-id $choice $weights-mat $features-vec $bias $observed-label)
   (let $p (ledger:affine-sigmoid-prob $weights-mat $features-vec $bias)
     (if (rc:unsupported? $p) $p
       (let $q (rc:rat-sub (Rat 1 1) $p)
         (if (== $observed-label true)
             (ledger:channel-likelihood-obs $obs-id $channel-id $choice $q $p)
             (if (== $observed-label false)
                 (ledger:channel-likelihood-obs $obs-id $channel-id $choice $p $q)
                 (RC:Unsupported invalid-neural-label $observed-label)))))))

(= (ledger:neural-bernoulli-obs $obs-id $choice $weights-mat $features-vec $bias $observed-label)
   (ledger:channel-neural-bernoulli-obs
     $obs-id $obs-id $choice $weights-mat $features-vec $bias $observed-label))

; ----------------------------------------------------------------------------
; 2. Catalogue Construction & Unnormalized Weight Synthesis
; ----------------------------------------------------------------------------

(= (ledger:build-comp-catalogue $raw-comps $comp-priors $comp-idx $acc)
   (if (== (size-atom $raw-comps) 0) (Ledger:CatOk $acc)
     (let* (((RC:Component $name $choices $priv $rules) (car-atom $raw-comps))
            ($priors (car-atom $comp-priors)))
       (if (or (not (rc:is-expr $priors)) (!= (size-atom $priors) (size-atom $choices)))
           (RC:Unsupported component-prior-count-mismatch ($name $choices $priors))
           (let $entries (ledger:zip-choice-priors (comp $comp-idx) $choices $priors 0 ())
             (if (rc:unsupported? $entries) $entries
               (ledger:build-comp-catalogue
                 (cdr-atom $raw-comps) (cdr-atom $comp-priors) (+ $comp-idx 1)
                 (union-atom $acc $entries))))))))

(= (ledger:zip-choice-priors $owner $choices $priors $pos $acc)
   (if (== (size-atom $choices) 0) $acc
     (let* (($ch (car-atom $choices))
            ($pr (car-atom $priors)))
       (if (not (rc:rat-prob-valid? $pr))
           (RC:Unsupported invalid-prior-probability ($ch $pr))
           (let (Rat $n $d) $pr
             (ledger:zip-choice-priors
               $owner (cdr-atom $choices) (cdr-atom $priors) (+ $pos 1)
               (union-atom $acc ((Ledger:Prior $ch $owner $pos (rc:rat $n $d))))))))))

(= (ledger:from-prepared $prepared $comp-prior-lists $shared-priors)
   (if (rc:unsupported? $prepared) $prepared
     (let (RC:Prepared $prog $w $fams $ni $sc $cols $pqs $ccomps) $prepared
       (let (RC:Program $boundary $shared $raw-comps) $prog
         (if (or (not (rc:is-expr $comp-prior-lists))
                 (!= (size-atom $comp-prior-lists) (size-atom $raw-comps)))
             (RC:Unsupported component-prior-list-mismatch
               ((size-atom $raw-comps) $comp-prior-lists))
             (if (or (not (rc:is-expr $shared-priors))
                     (!= (size-atom $shared-priors) (size-atom $shared)))
                 (RC:Unsupported shared-prior-count-mismatch ($shared $shared-priors))
                 (let $sh-cat (ledger:zip-choice-priors shared $shared $shared-priors 0 ())
                   (if (rc:unsupported? $sh-cat) $sh-cat
                     (let $c-cat (ledger:build-comp-catalogue $raw-comps $comp-prior-lists 0 ())
                       (if (rc:unsupported? $c-cat) $c-cat
                         (let (Ledger:CatOk $comp-entries) $c-cat
                           ; (Ledger:State prepared prior_catalogue immutable_obs_catalogue active_obs)
                           (Ledger:State
                             $prepared
                             (union-atom $sh-cat $comp-entries)
                             ()
                             ()))))))))))))

(= (ledger:find-choice $choice $catalogue)
   (if (== (size-atom $catalogue) 0) Ledger:UnknownChoice
     (let* (($entry (car-atom $catalogue))
            ((Ledger:Prior $ch $owner $pos $prior) $entry))
       (if (== $ch $choice) $entry
         (ledger:find-choice $choice (cdr-atom $catalogue))))))

(= (ledger:find-comp-index $comp-name $raw-comps $idx)
   (if (== (size-atom $raw-comps) 0) -1
     (let (RC:Component $name $ch $pr $rl) (car-atom $raw-comps)
       (if (== $name $comp-name) $idx
         (ledger:find-comp-index $comp-name (cdr-atom $raw-comps) (+ $idx 1))))))

(= (ledger:obs-id $entry)
   (case $entry
     (((Ledger:Obs $id $chan $ch $la $lp) $id)
      ((Ledger:SymptomObs $id $chan $cn $pa $la $lp) $id))))

(= (ledger:obs-channel $entry)
   (case $entry
     (((Ledger:Obs $id $chan $ch $la $lp) $chan)
      ((Ledger:SymptomObs $id $chan $cn $pa $la $lp) $chan))))

(= (ledger:find-obs-by-id $obs-id $obs-list)
   (if (== (size-atom $obs-list) 0) Ledger:ObsAbsent
     (let $obs (car-atom $obs-list)
       (if (== (ledger:obs-id $obs) $obs-id) $obs
         (ledger:find-obs-by-id $obs-id (cdr-atom $obs-list))))))

(= (ledger:find-obs-by-channel $channel-id $obs-list)
   (if (== (size-atom $obs-list) 0) Ledger:ChannelFree
     (let $obs (car-atom $obs-list)
       (if (== (ledger:obs-channel $obs) $channel-id) $obs
         (ledger:find-obs-by-channel $channel-id (cdr-atom $obs-list))))))

(= (ledger:remove-obs-by-id $obs-id $active-obs)
   (filter-atom $active-obs $obs (!= (ledger:obs-id $obs) $obs-id)))

(= (ledger:choice-weights $choice $prior $active-obs)
   (let* (($p1 $prior)
          ($p0 (rc:rat-sub (Rat 1 1) $p1)))
     (foldl-atom $active-obs ($p0 $p1) $cur $obs
       (case $obs
         (((Ledger:Obs $id $chan $ch $la $lp)
           (if (== $ch $choice)
               (let ($wa $wp) $cur
                 ((rc:rat-mul $wa $la) (rc:rat-mul $wp $lp)))
               $cur))
          ($symptom $cur))))))

(= (ledger:comp-symptoms $comp-name $active-obs)
   (filter-atom $active-obs $obs
     (case $obs
       (((Ledger:SymptomObs $id $chan $cn $pa $la $lp) (== $cn $comp-name))
        ($other false)))))

(= (ledger:apply-symptom-tables $base-worlds $scases $symptom-entries $boundary $shared $comp)
   (let $n-worlds (size-atom $base-worlds)
     (foldl-atom $symptom-entries
       (map-atom (rc:range 0 $scases) $h $base-worlds)
       $per-h $sym
       (let (Ledger:SymptomObs $id $chan $cn $pa $la $lp) $sym
         (let $table (rc:component-isolated-probe-table $boundary $shared $comp $pa)
           (map-atom (rc:range 0 $scases) $h
             (let* (($cur-h (index-atom $per-h $h))
                    ($tab-h (index-atom $table $h)))
               (map-atom (rc:range 0 $n-worlds) $w
                 (rc:rat-mul
                   (index-atom $cur-h $w)
                   (if (index-atom $tab-h $w) $lp $la))))))))))

(= (ledger:owner-weighted-state $owner $prepared $catalogue $active-obs)
   (let* (($owned
            (filter-atom $catalogue $entry
              (let (Ledger:Prior $ch $ow $pos $pr) $entry (== $ow $owner))))
          ($pairs
            (map-atom $owned $entry
              (let (Ledger:Prior $ch $ow $pos $pr) $entry
                (ledger:choice-weights $ch $pr $active-obs))))
          ($base-state (RC:WeightedState $pairs)))
     (case $owner
       ((shared $base-state)
        ((comp $idx)
         (let (RC:Prepared $prog $w $fams $ni $sc $cols $pqs $ccomps) $prepared
           (let (RC:Program $boundary $shared $raw-comps) $prog
             (let* (($comp (index-atom $raw-comps $idx))
                    ((RC:Component $cname $cchoices $cpriv $crules) $comp)
                    ($symptoms (ledger:comp-symptoms $cname $active-obs)))
               (if (== (size-atom $symptoms) 0)
                   $base-state
                   (let* (($base-worlds (rc:world-weights-rat $base-state))
                          ($per-h (ledger:apply-symptom-tables
                                    $base-worlds $sc $symptoms $boundary $shared $comp)))
                     (RC:ConditionedWorldState (size-atom $cchoices) $per-h)))))))))))

; ----------------------------------------------------------------------------
; 3. Bound Engine Lifecycle: Activate, Replay, Retract, Union & Scenario Change
; ----------------------------------------------------------------------------

(= (ledger:bind-engine $ledger)
   (if (rc:unsupported? $ledger) $ledger
     (let (Ledger:State $prepared $catalogue $obs-cat $active-obs) $ledger
       (let (RC:Prepared $prog $w $fams $ni $sc $cols $pqs $ccomps) $prepared
         (let* (($n-comps (size-atom $ccomps))
                ($comp-states
                  (map-atom (rc:range 0 $n-comps) $i
                    (ledger:owner-weighted-state (comp $i) $prepared $catalogue $active-obs)))
                ($sh-state
                  (ledger:owner-weighted-state shared $prepared $catalogue $active-obs))
                ($eng (rc:engine-new-with-shared $prepared $comp-states $sh-state)))
           (if (rc:unsupported? $eng) $eng
             (Ledger:BoundEngine $ledger $eng)))))))

(= (ledger:resolve-obs-owner $obs $prepared $cat)
   (case $obs
     (((Ledger:Obs $obs-id $chan $choice $la $lp)
       (case (ledger:find-choice $choice $cat)
         ((Ledger:UnknownChoice
           (RC:Unsupported unknown-choice-in-observation ($obs-id $choice)))
          ((Ledger:Prior $ch $owner $pos $prior)
           (Ledger:OwnerOk $obs-id $chan $owner)))))
      ((Ledger:SymptomObs $obs-id $chan $comp-name $priv-atom $la $lp)
       (let (RC:Prepared $prog $w $fams $ni $sc $cols $pqs $ccomps) $prepared
         (let (RC:Program $boundary $shared $raw-comps) $prog
           (let $cidx (ledger:find-comp-index $comp-name $raw-comps 0)
             (if (< $cidx 0)
                 (RC:Unsupported unknown-component-in-symptom ($obs-id $comp-name))
                 (let (RC:Component $cn $ch $priv $rl) (index-atom $raw-comps $cidx)
                   (if (not (rc:member? $priv-atom $priv))
                       (RC:Unsupported symptom-atom-not-private ($comp-name $priv-atom))
                       (Ledger:OwnerOk $obs-id $chan (comp $cidx)))))))))
      ($other (RC:Unsupported malformed-observation $other)))))

(= (ledger:observe $bound $obs)
   (if (rc:unsupported? $bound) $bound
     (if (rc:unsupported? $obs) $obs
       (let (Ledger:BoundEngine (Ledger:State $prep $cat $obs-cat $active) $eng) $bound
         (let $resolved (ledger:resolve-obs-owner $obs $prep $cat)
           (if (rc:unsupported? $resolved) $resolved
             (let (Ledger:OwnerOk $obs-id $chan $owner) $resolved
               ; 1. Immutable observation-ID check across entire append-only catalogue
               (let $reg (ledger:find-obs-by-id $obs-id $obs-cat)
                 (if (and (!= $reg Ledger:ObsAbsent) (!= $reg $obs))
                     (RC:Unsupported conflicting-observation-id ($obs-id $reg $obs))
                     ; 2. Active replay check
                     (case (ledger:find-obs-by-id $obs-id $active)
                       ((Ledger:ObsAbsent
                         ; 3. Single-draw noisy channel independence check
                         (let $chan-hit
                              (if (== $chan Ledger:HardDeterministic)
                                  Ledger:ChannelFree
                                  (ledger:find-obs-by-channel $chan $active))
                           (if (!= $chan-hit Ledger:ChannelFree)
                               (RC:Unsupported correlated-same-channel-observation
                                 ($chan (ledger:obs-id $chan-hit) $obs-id))
                               (let* (($obs-cat2
                                        (if (== $reg Ledger:ObsAbsent)
                                            (union-atom $obs-cat ($obs))
                                            $obs-cat))
                                      ($active2 (union-atom $active ($obs)))
                                      ($ledger2 (Ledger:State $prep $cat $obs-cat2 $active2))
                                      ($eng2 (ledger:refresh-owner $eng $owner $prep $cat $active2)))
                                 (Ledger:Step Activated (Ledger:BoundEngine $ledger2 $eng2))))))
                        ($existing
                         (Ledger:Step Replayed $bound)))))))))))))

; Re-activates an observation already registered in the immutable catalogue by ID
(= (ledger:observe-id $bound $obs-id)
   (if (rc:unsupported? $bound) $bound
     (if (not (rc:is-symbol $obs-id))
         (RC:Unsupported non-symbol-observation-id $obs-id)
         (let (Ledger:BoundEngine (Ledger:State $prep $cat $obs-cat $active) $eng) $bound
           (case (ledger:find-obs-by-id $obs-id $obs-cat)
             ((Ledger:ObsAbsent
               (RC:Unsupported unknown-observation-id $obs-id))
              ($registered
               (ledger:observe $bound $registered))))))))

(= (ledger:retract $bound $obs-id)
   (if (rc:unsupported? $bound) $bound
     (if (not (rc:is-symbol $obs-id))
         (RC:Unsupported non-symbol-observation-id $obs-id)
         (let (Ledger:BoundEngine (Ledger:State $prep $cat $obs-cat $active) $eng) $bound
           (case (ledger:find-obs-by-id $obs-id $active)
             ((Ledger:ObsAbsent
               (Ledger:Step NotPresent $bound))
              ($existing
               (let* (((Ledger:OwnerOk $oid $chan $owner) (ledger:resolve-obs-owner $existing $prep $cat))
                      ($active2 (ledger:remove-obs-by-id $obs-id $active))
                      ; Preserve $obs-cat so $obs-id remains permanently bound to its registered payload
                      ($ledger2 (Ledger:State $prep $cat $obs-cat $active2))
                      ($eng2 (ledger:refresh-owner $eng $owner $prep $cat $active2)))
                 (Ledger:Step Retracted (Ledger:BoundEngine $ledger2 $eng2))))))))))

(= (ledger:refresh-owner $eng $owner $prep $cat $active)
   (let $wstate (ledger:owner-weighted-state $owner $prep $cat $active)
     (case $owner
       ((shared (rc:engine-update $eng () $wstate))
        ((comp $idx) (rc:engine-update $eng (($idx $wstate)) keep))))))

(= (ledger:observe-all $bound $obs-list)
   (if (== (size-atom $obs-list) 0) $bound
     (let ($first $rest) (decons-atom $obs-list)
       (let $step (ledger:observe $bound $first)
         (if (rc:unsupported? $step) $step
           (let (Ledger:Step $tag $bound2) $step
             (ledger:observe-all $bound2 $rest)))))))

; Local component scenario replacement synchronized with the evidence ledger
(= (ledger:replace-component $bound $comp-idx $new-comp $new-priors)
   (if (rc:unsupported? $bound) $bound
     (let (Ledger:BoundEngine (Ledger:State $prep $cat $obs-cat $active) $eng) $bound
       (let (RC:Prepared $prog $w $fams $ni $sc $cols $pqs $ccomps) $prep
         (let (RC:Program $boundary $shared $raw-comps) $prog
           (if (or (not (rc:is-int $comp-idx))
                   (or (< $comp-idx 0) (>= $comp-idx (size-atom $raw-comps))))
               (RC:Unsupported unknown-component-index $comp-idx)
               (let (RC:Component $old-name $old-ch $old-priv $old-rl) (index-atom $raw-comps $comp-idx)
                 (case $new-comp
                   (((RC:Component $name $new-choices $new-priv $new-rules)
                     (if (or (not (rc:is-expr $new-priors))
                             (!= (size-atom $new-priors) (size-atom $new-choices)))
                         (RC:Unsupported replacement-prior-count-mismatch ($name $new-choices $new-priors))
                         (let* (($removed-choices
                                  (filter-atom $old-ch $ch (not (rc:member? $ch $new-choices))))
                                ($orphaned-choice-obs
                                  (filter-atom $active $obs
                                    (case $obs
                                      (((Ledger:Obs $id $chan $och $la $lp) (rc:member? $och $removed-choices))
                                       ($other false)))))
                                ($orphaned-sym-obs
                                  (filter-atom (ledger:comp-symptoms $old-name $active) $sym
                                    (let (Ledger:SymptomObs $id $chan $cn $pa $la $lp) $sym
                                      (or (!= $name $old-name) (not (rc:member? $pa $new-priv)))))))
                           (if (> (size-atom $orphaned-choice-obs) 0)
                               (RC:Unsupported active-evidence-on-removed-choice $orphaned-choice-obs)
                               (if (> (size-atom $orphaned-sym-obs) 0)
                                   (RC:Unsupported active-symptom-on-removed-private-atom $orphaned-sym-obs)
                                   (let $new-entries
                                        (ledger:zip-choice-priors (comp $comp-idx) $new-choices $new-priors 0 ())
                                     (if (rc:unsupported? $new-entries) $new-entries
                                       (let* (($kept-cat
                                                (filter-atom $cat $e
                                                  (let (Ledger:Prior $ch $ow $pos $pr) $e (!= $ow (comp $comp-idx)))))
                                              ($cat2 (union-atom $kept-cat $new-entries))
                                              ; Provisional prepared with new raw component to evaluate isolated symptom probes
                                              ($raw-comps-prov (rc:replace-at $raw-comps $comp-idx $new-comp))
                                              ($prep-prov
                                                (RC:Prepared (RC:Program $boundary $shared $raw-comps-prov)
                                                             $w $fams $ni $sc $cols $pqs $ccomps))
                                              ($wstate (ledger:owner-weighted-state (comp $comp-idx) $prep-prov $cat2 $active))
                                              ($eng2 (rc:engine-replace-component $eng $comp-idx $new-comp $wstate)))
                                         (if (rc:unsupported? $eng2) $eng2
                                           (Ledger:BoundEngine
                                             (Ledger:State (rc:engine-prepared $eng2) $cat2 $obs-cat $active)
                                             $eng2))))))))))
                    ($other (RC:Unsupported malformed-replacement-component $other)))))))))))

; Query & inspection wrappers on Ledger:BoundEngine
(= (ledger:query (Ledger:BoundEngine $l $eng) $q)
   (rc:query $eng $q))

(= (ledger:query-exact (Ledger:BoundEngine $l $eng) $q)
   (rc:query-exact $eng $q))

(= (ledger:engine (Ledger:BoundEngine $l $eng)) $eng)
(= (ledger:active-observations (Ledger:BoundEngine (Ledger:State $p $c $oc $act) $e)) $act)
(= (ledger:registered-observations (Ledger:BoundEngine (Ledger:State $p $c $oc $act) $e)) $oc)
