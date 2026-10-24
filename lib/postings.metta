; A prepared snapshot of (key numeric-positions) groups. Group order and
; duplicate positions are observable. Preparation sorts positions once.
; Every snapshot has the same owned capability lifetime. Unsupported packing
; retains an ordinary snapshot and uses the authored observations below.
!(import! &self tensor)
!(import! &self postings_descriptors)
(: __cetta_lib_postings_prepare (-> Atom Atom))
(: __cetta_lib_postings_own (-> Atom Atom))
(: __cetta_lib_postings_counts (-> Atom Atom Atom Atom))
(: __cetta_lib_postings_inspect (-> Atom Atom))
(: __cetta_lib_postings_close (-> Atom Atom))
(: __cetta_lib_postings_sources_current (-> Atom Atom))
(: postings:sources-current-reference (-> Atom %Undefined%))
(: postings:prepare (-> %Undefined% %Undefined%))
(: postings:prepare-blocks (-> %Undefined% %Undefined%))
(: postings:prepare-space-blocks-attempt (-> Atom Atom %Undefined%))
(: postings:inspect (-> %Undefined% %Undefined%))
(: postings:inspect-table (-> %Undefined% %Undefined%))
(: postings:counts-before (-> %Undefined% %Undefined% %Undefined%))
(: postings:counts (-> %Undefined% %Undefined%))
(: postings:table-counts-before (-> %Undefined% Atom %Undefined% %Undefined% %Undefined%))
(: postings:table-counts (-> %Undefined% Atom %Undefined% %Undefined%))
(: postings:table-counts-tensor-before (-> %Undefined% Atom %Undefined% %Undefined% %Undefined%))
(: postings:table-counts-tensor (-> %Undefined% Atom %Undefined% %Undefined%))
(: postings:close (-> %Undefined% %Undefined%))
(: postings:stats (-> %Undefined% %Undefined%))
(: postings:sources-current (-> %Undefined% %Undefined%))
(: postings:columns-from-rows (-> %Undefined% %Undefined% %Undefined%))

; A revision observation, not a cached verdict or a certificate. Every pair
; names the actual source and its expected revision. Unsupported carriers
; and bounded-fuel evaluation use the authored all-row observation below.
(= (postings:sources-current $guards)
   (function
     (let $rows (postings:sequence $guards)
       (case (__cetta_lib_postings_sources_current $rows)
         ((Postings:Fallback
            (let $result (postings:sources-current-reference $rows)
              (return $result)))
          ($result (return $result)))))))
(= (postings:sources-current-reference $guards)
   (let $rows (postings:sequence $guards)
     (let $stale
       (filter-atom $rows $guard
         (let $pair (postings:sequence $guard)
           (let ($source $revision) $pair
             (not (== $revision (_minimal-space-revision $source))))))
       (== $stale ()))))

(= (postings:sequence $value)
   (if (== (get-metatype $value) List)
     (__cetta_lib_list_to_expression $value) $value))
(= (postings:prepare $groups)
   (function
     (let $items (postings:sequence $groups)
       (case (__cetta_lib_postings_prepare $items)
         (((NativeHandle "postings-index-v1" $ready-id)
            (return (NativeHandle "postings-index-v1" $ready-id)))
          ($unsorted
            (let $sorted (map-atom $items $row
              (let $pair (postings:sequence $row)
                (let ($key $positions) $pair
                  (let* (($position-items (postings:sequence $positions))
                         ($numbers (sort-numbers-atom $position-items)))
                    ($key $numbers)))))
              (case (__cetta_lib_postings_prepare $sorted)
                (((NativeHandle "postings-index-v1" $packed-id)
                   (return (NativeHandle "postings-index-v1" $packed-id)))
                 ($other
                   (let $owner (postings:own $sorted)
                     (return $owner))))))))))))
(= (postings:own $groups)
   (function (chain (eval (__cetta_lib_postings_own $groups)) $result (return $result))))

; The group order is canonical key order, not first event order. Repeated
; events remain repeated positions. Unsupported packing keeps this meaning.
(= (postings:prepare-events $events)
   (function
     (case (__cetta_lib_postings_prepare (Postings:Events $events))
       (((NativeHandle "postings-index-v1" $id) (return (NativeHandle "postings-index-v1" $id)))
        ($other
          (let* (($all-keys (map-atom $events $event (car-atom $event)))
                 ($unique-keys (unique-atom $all-keys))
                 ($keys (sort-atom $unique-keys))
                 ($groups (map-atom $keys $key
                   (let* (($matching (filter-atom $events $event
                              (let $event-key (car-atom $event) (unify $event-key $key True False))))
                          ($positions (map-atom $matching $event (index-atom $event 1))))
                     ($key $positions))))
                 ($index (postings:prepare $groups)))
            (return $index)))))))

; Each block is (outer-keys group position). Every key occurrence contributes
; one occurrence to that numeric group. This is the compact event form of an
; inverted relation; preparation does not erase repeated keys or events.
(= (postings:prepare-blocks $blocks)
   (function
     (let $items (postings:sequence $blocks)
       (case (__cetta_lib_postings_prepare (Postings:Blocks $items))
         (((NativeHandle "postings-table-v1" $id)
            (return (NativeHandle "postings-table-v1" $id)))
          ($other
            (let $owner (postings:own (Postings:Blocks $items))
              (return $owner))))))))

; A declared projection may borrow an exact immutable space snapshot and feed
; the same table builder without first constructing its ordinary block tree.
; This is deliberately an attempt: its caller owns the authored fallback.
; (Postings:BlockLayout head row-width keys-field group-field position-field
;   prefix-keys forbidden-heads) uses zero-based fields, including the head
; at zero. Row width counts the head too. A mismatched width declines before
; projection; this does not broaden the caller's exact constructor match.
(= (postings:prepare-space-blocks-attempt $source $layout)
   (function
     (chain
       (eval (__cetta_lib_postings_prepare
               (Postings:SpaceBlocks $source $layout)))
       $result (return $result))))

(= (postings:block-events $blocks $key)
   (let $items (postings:sequence $blocks)
     (if (== $items ()) ()
       (let ($block $rest) (decons-atom $items)
         (let ($keys $group $position) (postings:sequence $block)
           (postings:block-key-events (postings:sequence $keys) $key
             $group $position (postings:block-events $rest $key)))))))
(= (postings:block-key-events $keys $key $group $position $tail-events)
   (if (== $keys ()) $tail-events
     (let ($candidate $rest) (decons-atom $keys)
       (let $next
         (postings:block-key-events $rest $key $group $position $tail-events)
         (if (== $candidate $key)
           (eval (cons-atom ($group $position) $next)) $next)))))

(= (postings:block-keys $blocks)
   (let $items (postings:sequence $blocks)
     (if (== $items ()) ()
       (let ($block $rest) (decons-atom $items)
         (let ($keys $group $position) (postings:sequence $block)
           (postings:prepend-keys (postings:sequence $keys)
             (postings:block-keys $rest)))))))
(= (postings:prepend-keys $keys $tail)
   (if (== $keys ()) $tail
     (let ($key $rest) (decons-atom $keys)
       (let $next (postings:prepend-keys $rest $tail)
         (eval (cons-atom $key $next))))))

(= (postings:table-reference-index $handle $key)
   (case (__cetta_lib_postings_inspect $handle)
     (((Postings:Blocks $blocks)
        (postings:prepare-events (postings:block-events $blocks $key)))
      ((Postings:Fault $reason) (Postings:Fault $reason))
      ($table
        (let $events
          (collapse
            (let ($observed $groups) (superpose $table)
              (if (== $observed $key)
                (let ($group $positions) (superpose $groups)
                  (let $position (superpose $positions) ($group $position)))
                (superpose ()))))
          (postings:prepare-events $events))))))

(= (postings:table-reference-inspect $blocks)
   (let $all (postings:block-keys $blocks)
     (let $keys (unique-atom $all)
       (postings:inspect-table-keys $blocks $keys))))
(= (postings:inspect-table-keys $blocks $keys)
   (if (== $keys ()) ()
     (let ($key $rest) (decons-atom $keys)
       (let* (($groups (postings:inspect
                         (postings:prepare-events
                           (postings:block-events $blocks $key))))
              ($tail (postings:inspect-table-keys $blocks $rest)))
         (eval (cons-atom ($key $groups) $tail))))))

(= (postings:inspect-table (NativeHandle "postings-table-v1" $id))
   (case (__cetta_lib_postings_inspect
           (NativeHandle "postings-table-v1" $id))
     (((Postings:Blocks $blocks)
        (postings:table-reference-inspect $blocks))
      ($other $other))))

(= (postings:try-table-counts $handle $mode $request)
   (function
     (chain (eval (__cetta_lib_postings_counts $handle $mode $request))
       $result (return $result))))

(= (postings:table-counts-before
      (NativeHandle "postings-table-v1" $id) $key $bound $size)
   (let* (($handle (NativeHandle "postings-table-v1" $id))
          ($answer (postings:try-table-counts
                     $handle table-before ($key $bound $size))))
     (case $answer
       ((Postings:Fallback
          (postings:counts-before
            (postings:table-reference-index $handle $key) $bound))
        ($result $result)))))
(= (postings:table-counts
      (NativeHandle "postings-table-v1" $id) $key $size)
   (let* (($handle (NativeHandle "postings-table-v1" $id))
          ($answer (postings:try-table-counts
                     $handle table-all ($key 0 $size))))
     (case $answer
       ((Postings:Fallback
          (postings:counts
            (postings:table-reference-index $handle $key)))
        ($result $result)))))
(= (postings:table-counts-tensor-before
      (NativeHandle "postings-table-v1" $id) $key $bound $size)
   (let* (($handle (NativeHandle "postings-table-v1" $id))
          ($answer (postings:try-table-counts
                     $handle table-tensor-before ($key $bound $size))))
     (case $answer
       ((Postings:Fallback
          (postings:counts-tensor-before
            (postings:table-reference-index $handle $key) $bound $size))
        ($result $result)))))
(= (postings:table-counts-tensor
      (NativeHandle "postings-table-v1" $id) $key $size)
   (let* (($handle (NativeHandle "postings-table-v1" $id))
          ($answer (postings:try-table-counts
                     $handle table-tensor-all ($key 0 $size))))
     (case $answer
       ((Postings:Fallback
          (postings:counts-tensor
            (postings:table-reference-index $handle $key) $size))
        ($result $result)))))

; Each query is (key payload). Its positive prefix counts visit groups in
; numeric order; queries retain caller order. The callback receives the
; previous numeric state product, group, count and payload, and returns a new
; state product (optionally headed by a fixed constructor). Groups appear in
; first-encounter order. No reassociation or
; multiplication by counts is implicit: the callback authors that meaning.
; A packed state is an expression product. Other carriers (including native
; list values) use the authored fold without changing their metatype.
(= (postings:fold-table-before
      (NativeHandle "postings-table-v1" $id) $queries $bound $initial $step)
   (postings:fold-table-by-before (NativeHandle "postings-table-v1" $id)
     $queries $bound postings:group-identity $initial $step))
(= (postings:group-identity $group $payload) $group)

; The projection receives the original group and query payload. Equal projected
; keys share a state, while the step still receives the original group. This
; changes grouping, never the traversal schedule within a projected group.
(= (postings:fold-table-by-before
      (NativeHandle "postings-table-v1" $id) $queries $bound $project $initial $step)
   (let* (($handle (NativeHandle "postings-table-v1" $id))
          ($space (context-space))
          ($answer (postings:try-table-counts $handle table-fold-before
            ($space $queries $bound $project $initial $step))))
     (case $answer
       ((Postings:Fallback
          (postings:fold-table-by-reference $handle $queries $bound $project $initial $step))
        ($result $result)))))
(= (postings:fold-table-reference $handle $queries $bound $initial $step)
   (postings:fold-table-by-reference $handle $queries $bound
     postings:group-identity $initial $step))

; Observe numeric states as typed dense columns, retaining first-group order.
; The ordinary fold and explicit tensor constructors define the fallback.
(= (postings:fold-table-columns-before
      (NativeHandle "postings-table-v1" $id) $queries $bound $project $initial $step $dtypes)
   (let* (($handle (NativeHandle "postings-table-v1" $id))
          ($space (context-space))
          ($answer (postings:try-table-counts $handle table-fold-columns-before
            ($space $queries $bound $project $initial $step $dtypes))))
     (case $answer
       ((Postings:Fallback
          (postings:columns-from-rows $dtypes
            (postings:fold-table-by-reference $handle $queries $bound $project $initial $step)))
        ((Postings:Fault mode)
          (postings:columns-from-rows $dtypes
            (postings:fold-table-by-reference $handle $queries $bound $project $initial $step)))
        ($result $result)))))
(= (postings:column-fields $row)
   (let $pair (postings:sequence $row)
     (if (not (== (get-metatype $pair) Expression)) Postings:InvalidColumnState
       (if (not (== (size-atom $pair) 2)) Postings:InvalidColumnState
         (let $state (postings:sequence (index-atom $pair 1))
           (if (not (== (get-metatype $state) Expression)) Postings:InvalidColumnState
             (if (== (size-atom $state) 0) ()
               (let $head (car-atom $state)
                 (if (== (get-metatype $head) Symbol)
                   (cdr-atom $state) $state)))))))))
(= (postings:columns-from-rows $dtypes $rows)
   (case $rows
     (((Postings:Fault $reason) $rows)
      ($observed
        (let* (($items (postings:sequence $observed))
               ($types (postings:sequence $dtypes)))
          (if (not (and (== (get-metatype $items) Expression)
                        (== (get-metatype $types) Expression)))
            (Postings:Fault column-sequence)
            (let* (($count (size-atom $items))
                   ($width (size-atom $types))
                   ($fields (map-atom $items $row (postings:column-fields $row)))
                   ($invalid (filter-atom $fields $state
                     (if (== $state Postings:InvalidColumnState) True
                       (not (== (size-atom $state) $width))))))
              (if (not (== $invalid ())) (Postings:Fault column-state)
                (let* (($labels (tensor:from-values i64 ($count)
                         (map-atom $items $row (car-atom $row))))
                       (($column-count $columns)
                         (foldl-atom $types (0 ()) $progress $dtype
                           (let ($column $values) $progress
                             (let $value (tensor:from-values $dtype ($count)
                                   (map-atom $fields $state (index-atom $state $column)))
                               ((+ $column 1) (union-atom $values ($value))))))))
                  (postings:columns-validated $labels $types $columns))))))))))
(= (postings:columns-validated $labels $types $columns)
   (let ($count $valid)
     (foldl-atom $types (0 True) $progress $dtype
       (let ($index $valid) $progress
         ((+ $index 1)
           (and $valid (== (tensor:dtype (index-atom $columns $index)) $dtype)))))
     (if (and $valid (== (tensor:dtype $labels) i64))
       (postings:column-result $labels $columns) (Postings:Fault column-value))))
(= (postings:fold-table-by-reference $handle $queries $bound $project $initial $step)
   (case (postings:stats $handle)
     (((Postings:Fault $reason) (Postings:Fault $reason))
      ($valid
        (let* (($blocks (map-atom $queries $query
                  (let ($key $payload) $query
                    (let $counts (postings:table-counts-before $handle $key $bound 0)
                      (map-atom $counts $entry
                        (let ($group $count) $entry
                          (let $projected ($project $group $payload)
                            ($projected $group $count $payload))))))))
               ($events (foldl-atom $blocks () $out $block (union-atom $out $block)))
               ($group-keys (map-atom $events $event (index-atom $event 0)))
               ($groups (unique-atom $group-keys)))
          (map-atom $groups $group
            (let $state
              (foldl-atom $events $initial $state $entry
                (let ($observed $source-group $count $payload) $entry
                  (if (== $observed $group)
                    ($step $state $source-group $count $payload) $state)))
              ($group $state))))))))
(= (postings:inspect (NativeHandle "postings-index-v1" $id))
   (function (chain (eval (__cetta_lib_postings_inspect (NativeHandle "postings-index-v1" $id)))
     $result (return $result))))
(= (postings:counts-before (NativeHandle "postings-index-v1" $id) $bound)
   (let* (($handle (NativeHandle "postings-index-v1" $id))
          ($answer (postings:try-counts-before $handle $bound)))
     (case $answer
       ((Postings:Fallback
          (let $groups (postings:inspect $handle)
            (postings:reference-counts $groups $bound)))
        ($result $result)))))
(= (postings:try-counts-before $handle $bound)
   (function (chain (eval (__cetta_lib_postings_counts $handle before $bound))
     $result (return $result))))
(= (postings:counts (NativeHandle "postings-index-v1" $id))
   (let $handle (NativeHandle "postings-index-v1" $id)
     (case (postings:try-counts $handle)
       ((Postings:Fallback (postings:reference-all (postings:inspect $handle)))
        ($result $result)))))
(= (postings:try-counts $handle)
   (function (chain (eval (__cetta_lib_postings_counts $handle all 0))
     $result (return $result))))
(= (postings:close (NativeHandle "postings-index-v1" $id))
   (function (chain (eval (__cetta_lib_postings_close (NativeHandle "postings-index-v1" $id)))
     $result (return $result))))
(= (postings:close (NativeHandle "postings-table-v1" $id))
   (function (chain (eval (__cetta_lib_postings_close (NativeHandle "postings-table-v1" $id)))
     $result (return $result))))
(= (postings:stats (NativeHandle "postings-index-v1" $id))
   (function (chain (eval (__cetta_lib_postings_counts (NativeHandle "postings-index-v1" $id) stats 0))
     $result (return $result))))
(= (postings:stats (NativeHandle "postings-table-v1" $id))
   (function (chain (eval (__cetta_lib_postings_counts (NativeHandle "postings-table-v1" $id) stats 0))
     $result (return $result))))
(= (postings:reference-counts $groups $bound)
   (let $counts
     (map-atom $groups $row
       (let ($key $positions) $row
         (let* (($before (filter-atom $positions $position (< $position $bound)))
                ($count (size-atom $before)))
           ($key $count))))
     (filter-atom $counts $row
       (let $count (index-atom $row 1) (> $count 0)))))
(= (postings:reference-all $groups)
   (let $counts
     (map-atom $groups $row
       (let ($key $positions) $row
         (let $count (size-atom $positions) ($key $count))))
     (filter-atom $counts $row
       (let $count (index-atom $row 1) (> $count 0)))))

; Explicit numeric-coordinate observation: zero counts are absent; positive
; counts are stored. Shape and duplicate-coordinate validation belongs to the
; ordinary tensor constructor on the fallback route too.
(= (postings:counts-tensor-before $handle $bound $size)
   (case (postings:try-counts-tensor $handle tensor-before ($bound $size))
     ((Postings:Fallback
        (tensor:from-coo i64 ($size) (postings:counts-before $handle $bound)))
      ($result $result))))
(= (postings:counts-tensor $handle $size)
   (case (postings:try-counts-tensor $handle tensor-all (0 $size))
     ((Postings:Fallback
        (tensor:from-coo i64 ($size) (postings:counts $handle)))
      ($result $result))))
(= (postings:try-counts-tensor $handle $mode $request)
   (function (chain (eval (__cetta_lib_postings_counts $handle $mode $request))
     $result (return $result))))
