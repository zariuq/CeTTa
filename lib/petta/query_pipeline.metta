; Bounded row stages for the existing MeTTa query cursor. Callbacks are
; explicitly invoked pure, deterministic, single-answer MeTTa functions.
; They must not perform effects or hidden/external reads. Captured queries
; do not enroll arbitrary callback reads into a transaction automatically.

(: kbq:callback? (-> Atom Bool))
(: kbq:pipe-config? (-> Atom Atom Number Bool))
(: kbq:bounded-state (-> Atom Number %Undefined%))
(: kbq:begin-pipe (-> %Undefined% Atom Atom Atom Atom Number Number %Undefined%))
(: kbq:callback1 (-> Atom Atom %Undefined%))
(: kbq:callback2 (-> Atom Atom Atom %Undefined%))
(: kbq:advance-pipe (-> Atom Atom Atom Atom Number %Undefined%))
(: kbq:pipe-row (-> Atom Atom Atom Atom Number %Undefined%))
(: kbq:distinct-member (-> Atom Atom Atom %Undefined%))
(: kbq:group-row (-> Atom Atom Atom %Undefined%))
(: kbq:slice-row (-> Atom Atom Atom %Undefined%))
(: kbq:pipe-end (-> Atom Atom Atom Number Number %Undefined%))
(: kbq:sort-less (-> Atom Atom %Undefined%))

(= (kbq:callback? $name)
  ; A callable symbol's semantic metatype can be Grounded. Inspect its data
  ; encoding, as the ordinary Space index does, without invoking that symbol.
  (case (store:encode $name)
    (((StoreEncoded $body) (== (str:byte-slice $body 4 5) "S")) ($failure False))))
(= (kbq:pipe-config? $kind $config $maximum)
  (case $kind
    ((slice
      (let (KqSliceConfigV1 $offset $count) $config
        (and (and (kb:integer? $offset) (kb:integer? $count)) (and (>= $offset 0) (>= $count 0)))))
     (group
      (let (KqGroupConfigV1 $key $equal $initial $step) $config
        (and (and (kbq:callback? $key) (kbq:callback? $equal))
          (and (kbq:callback? $step) (kbq:state-budget? $maximum)))))
     ($other
      (and (kbq:callback? $config)
        (if (or (== $kind map) (== $kind select)) True (kbq:state-budget? $maximum)))))))
(= (kbq:state-budget? $maximum)
  (and (kb:integer? $maximum) (and (> $maximum 0) (<= $maximum 1048576))))
(= (kbq:bounded-state $value $maximum)
  (case (store:encode $value)
    (((StoreEncoded $body)
      (if (<= (str:byte-length $body) $maximum) (RelOk $value)
        (Error QueryState (QueryStateTooLarge $maximum))))
     ($failure (Error QueryState (QueryStateCodec $failure))))))
(= (kbq:begin-pipe $view $source $kind $config $initial $maximum $bytes)
  (if (kbq:pipe-config? $kind $config $maximum)
    (case (if (> $maximum 0) (kbq:bounded-state $initial $maximum) (RelOk $initial))
      (((RelOk $data)
        (if (and (== $kind slice) (kbq:zero-slice? $config))
         (KqReady (KqRowsState ()))
         (case (kbq:begin $view $source $bytes)
          (((KqReady $active) (KqReady (KqPipeStateV1 $kind $active $config $data $maximum)))
           ($failure $failure)))))
       ($failure (KbRefused $failure))))
    (KbRefused query-pipeline-config)))
(= (kbq:zero-slice? $config)
  (case $config (((KqSliceConfigV1 $offset $count) (== $count 0)) ($other False))))

; A callback may bind its arguments. Freshen those arguments before calling,
; so a filter/comparator cannot bind a source row or its continuation tail.
(= (kbq:callback1 $callback $row)
  (case (store:encode $row)
    (((StoreEncoded $body)
      (case (store:decode $body)
        (((StoreDecoded $copy) (rel-i:call1 $callback $copy))
         ($failure (Error QueryCallback $failure)))))
     ($failure (Error QueryCallback $failure)))))
(= (kbq:callback2 $callback $left $right)
  (case (store:encode (KqArgumentsV1 $left $right))
    (((StoreEncoded $body)
      (case (store:decode $body)
        (((StoreDecoded (KqArgumentsV1 $a $b)) (rel-i:call2 $callback $a $b))
         ($failure (Error QueryCallback $failure)))))
     ($failure (Error QueryCallback $failure)))))

(= (kbq:advance-pipe $kind $active $config $data $maximum)
  (case (kbq:advance $active)
    (((KqOut $row $next $keys $bytes)
      (case (kbq:pipe-row $kind $config $data $row $maximum)
        (((KqPipeOutV1 $value $state)
          (KqOut $value (KqPipeStateV1 $kind $next $config $state $maximum) $keys $bytes))
         ((KqPipeSkipV1 $state)
          (KqSkip (KqPipeStateV1 $kind $next $config $state $maximum) $keys $bytes))
         ((KqPipeStopV1 $value)
          (let $_ (kbq:close-state $next) (KqOut $value (KqRowsState ()) $keys $bytes)))
         (KqPipeStopEmptyV1 (let $_ (kbq:close-state $next) (KqEnd $keys $bytes)))
         ($failure (let $_ (kbq:close-state $next) (KqFail $failure $keys $bytes))))))
     ((KqSkip $next $keys $bytes) (KqSkip (KqPipeStateV1 $kind $next $config $data $maximum) $keys $bytes))
     ((KqEnd $keys $bytes) (kbq:pipe-end $kind $config $data $keys $bytes))
     ($failure $failure))))
(= (kbq:pipe-row $kind $config $data $row $maximum)
  (case $kind
    ((map
      (case (kbq:callback1 $config $row)
        (((RelOk $value) (KqPipeOutV1 $value $data)) ($failure $failure))))
     (select
      (case (rel-i:truth (kbq:callback1 $config $row))
        ((RelTrue (KqPipeOutV1 $row $data))
         (RelFalse (KqPipeSkipV1 $data)) (RelUnknown (KqPipeSkipV1 $data)) ($failure $failure))))
     (distinct
      (case (kbq:distinct-member $config $row $data)
        ((True (KqPipeSkipV1 $data))
         (False
          (let $candidate (cons-atom $row $data)
            (case (kbq:bounded-state $candidate $maximum)
              (((RelOk $next) (KqPipeOutV1 $row $next)) ($failure $failure)))))
         ($failure $failure))))
     (fold
      (case (kbq:callback2 $config $data $row)
        (((RelOk $value)
          (case (kbq:bounded-state $value $maximum)
            (((RelOk $next) (KqPipeSkipV1 $next)) ($failure $failure))))
         ($failure $failure))))
     (group
      (case (kbq:group-row $config $data $row)
        (((RelOk $value)
          (case (kbq:bounded-state $value $maximum)
            (((RelOk $next) (KqPipeSkipV1 $next)) ($failure $failure))))
         ($failure $failure))))
     (order
      (let $candidate (cons-atom (KqSortRowV1 $config $row) $data)
        (case (kbq:bounded-state $candidate $maximum)
          (((RelOk $next) (KqPipeSkipV1 $next)) ($failure $failure)))))
     (slice (kbq:slice-row $config $data $row))
     ($bad (Error QueryPipeline invalid-kind)))))
(= (kbq:distinct-member $equal $row $seen)
  (if (== $seen ()) False
    (let ($head $tail) (decons-atom $seen)
      (case (rel-i:bool (kbq:callback2 $equal $head $row))
        ((True True) (False (kbq:distinct-member $equal $row $tail)) ($failure $failure))))))
(= (kbq:group-row $config $groups $row)
  (case (store:encode (KqGroupArgumentsV1 $config $groups $row))
    (((StoreEncoded $body)
      (case (store:decode $body)
        (((StoreDecoded (KqGroupArgumentsV1 (KqGroupConfigV1 $key $equal $initial $step) $copy $value))
          (rel-i:groups $key $equal $initial $step (cons-atom $value ()) $copy))
         ($failure (Error QueryGroup $failure)))))
     ($failure (Error QueryGroup $failure)))))
(= (kbq:slice-row (KqSliceConfigV1 $offset $count) $state $row)
  (let (KqSliceProgressV1 $skip $keep)
    (if (== $state ()) (KqSliceProgressV1 $offset $count) $state)
    (if (== $keep 0) KqPipeStopEmptyV1
      (if (> $skip 0) (KqPipeSkipV1 (KqSliceProgressV1 (- $skip 1) $keep))
        (if (== $keep 1) (KqPipeStopV1 $row)
          (KqPipeOutV1 $row (KqSliceProgressV1 0 (- $keep 1))))))))

(= (kbq:pipe-end $kind $config $data $keys $bytes)
  (case $kind
    ((fold (KqOut $data (KqRowsState ()) $keys $bytes))
     (group (KqSkip (KqRowsState $data) $keys $bytes))
     (order
      (case (rel-i:order kbq:sort-less (list:reverse $data))
        (((RelOk $sorted)
          (KqSkip (KqRowsState (map-atom $sorted $entry
            (let (KqSortRowV1 $less $row) $entry $row))) $keys $bytes))
         ($failure (KqFail $failure $keys $bytes)))))
     ($other (KqEnd $keys $bytes)))))
; Equal primary keys are ordered by canonical CAV1 row bytes. Carried
; occurrence references are therefore stable tie breakers. Equal projected
; rows remain indistinguishable duplicates, never implicitly distinct.
(= (kbq:sort-less (KqSortRowV1 $less $left) (KqSortRowV1 $same $right))
  (case (rel-i:bool (kbq:callback2 $less $left $right))
    ((True True)
     (False
      (case (rel-i:bool (kbq:callback2 $less $right $left))
        ((True False)
         (False
          (case (store:encode $left)
            (((StoreEncoded $a)
              (case (store:encode $right)
                (((StoreEncoded $b) (< (str:compare $a $b) 0))
                 ($failure (Error QueryOrder $failure)))))
             ($failure (Error QueryOrder $failure)))))
         ($failure $failure))))
     ($failure $failure))))
