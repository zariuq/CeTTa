; Immutable packed numerical values. Native primitives are activated by the
; ordinary library registry. Unrecognized types remain unreduced, so authored
; extensions may interpret them. Recognized invalid tensors yield Tensor:Fault.
; Construction names its destination dtype explicitly. Coordinate observations
; distinguish tensor:absent from (tensor:present 0).
;
; Reductions name their schedule: sequential is stored-coordinate order;
; fixed4 explicitly selects four accumulation lanes. Floating reductions return
; f64. Missing entries are not silently filled with floating-point zeroes.
; Inspection materializes scalar atoms only when requested.
; from-json validates all input and the selected rectangular shape. Its scalar
; projection admits finite numeric values (or JSON booleans for bool), refusing
; nonfinite conversion. Numeric spellings supply the requested scalar dtype;
; this is not an obligatory ordinary-DOM intermediate.
!(import! &self tensor_descriptors)
(: tensor:from-values (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:from-json (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:from-coo (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:from-coo (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:from-coordinates (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:from-coordinates (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:inspect (-> %Undefined% %Undefined%))
(: tensor:is-tensor (-> %Undefined% %Undefined%))
(: tensor:entries (-> %Undefined% %Undefined%))
(: tensor:gather (-> %Undefined% %Undefined% %Undefined%))
(: tensor:gather-rows (-> %Undefined% %Undefined% %Undefined%))
; Selection is strict in its three tensor operands, not a lazy code branch.
; Shapes agree exactly; missing condition/selected support remains absent.
(: tensor:where (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:scan-sum (-> %Undefined% %Undefined% %Undefined%))
(: tensor:concatenate (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:permute (-> %Undefined% %Undefined% %Undefined%))
(: tensor:merge (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:zip-map (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined%))
(: __cetta_lib_tensor_try_zip_map (-> Atom Atom Atom Atom Atom))
(: tensor:shape (-> %Undefined% %Undefined%))
(: tensor:dtype (-> %Undefined% %Undefined%))
(: tensor:layout (-> %Undefined% %Undefined%))
(: tensor:at (-> %Undefined% %Undefined% %Undefined%))
(: tensor:copy (-> %Undefined% %Undefined%))
(: tensor:convert (-> %Undefined% %Undefined% %Undefined%))
(: tensor:cast (-> %Undefined% %Undefined% %Undefined%))
(: tensor:cast (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:promote (-> %Undefined% %Undefined% %Undefined%))
(: tensor:view (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:reshape (-> %Undefined% %Undefined% %Undefined%))
(: tensor:sum (-> %Undefined% %Undefined% %Undefined%))
(: tensor:sum (-> %Undefined% %Undefined%))
(: tensor:matvec (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:matvec (-> %Undefined% %Undefined% %Undefined%))
(: tensor:top-k (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:top-k (-> %Undefined% %Undefined% %Undefined%))
(: tensor:encode (-> %Undefined% %Undefined%))
(: tensor:decode (-> %Undefined% %Undefined%))
; CTN1 interchange through packed U8 values, with identical framing to the
; binary-string interface. Arbitrary payload bytes are never treated as UTF-8.
(: tensor:encode-bytes (-> %Undefined% %Undefined%))
(: tensor:decode-bytes (-> %Undefined% %Undefined%))
(: tensor:builder (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:builder (-> %Undefined% %Undefined% %Undefined%))
(: tensor:put (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:freeze (-> %Undefined% %Undefined%))
(: tensor:freeze-log (-> %Undefined% %Undefined%))
(: tensor:close (-> %Undefined% %Undefined%))
(: tensor:all-finite (-> %Undefined% %Undefined%))
(: tensor:normalize (-> %Undefined% %Undefined%))
(: tensor:cosine-index (-> %Undefined% %Undefined%))
(: tensor:cosine-index (-> %Undefined% %Undefined% %Undefined%))
(: tensor:cosine-scores (-> %Undefined% %Undefined% %Undefined%))
(: tensor:cosine-retrieve (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:cosine-retrieve (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:index-source (-> %Undefined% %Undefined%))
(: tensor:screen-index (-> %Undefined% %Undefined%))
(: tensor:screen (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:screen (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined%))
(: tensor:screen-candidates (-> %Undefined% %Undefined%))
(: tensor:screen-info (-> %Undefined% %Undefined%))
(: tensor:rerank (-> %Undefined% %Undefined%))

; Ordinary authored observers. New scripts may compose operations without
; adding a native callback. These also serve as independent scalar references.
; Fallback evaluates in the ambient call space. Its control token is plain
; data, so a persistent continuation never has to retain a borrowed Space.
(= (tensor:zip-map $dtype $inputs $projection $captures)
   (function
     (case (__cetta_lib_tensor_try_zip_map $dtype $inputs $projection $captures)
       (((Tensor:Mapped $result) (return $result))
        (Tensor:Fallback (let $result (tensor:zip-map-reference $dtype $inputs $projection $captures)
                  (return $result)))))))
(= (tensor:same-index-sequence $left $right)
   (if (== (size-atom $left) (size-atom $right)) (== $left $right) False))
(= (tensor:zip-map-reference $dtype $inputs $projection $captures)
   (let ($first $rest) (decons-atom $inputs)
     (let (TensorData $first-dtype $shape $entries) (tensor:inspect $first)
       (let* (($coordinates (map-atom $entries $entry (car-atom $entry)))
              ($same (foldl-atom $rest True $valid $input
                (let (TensorData $input-dtype $input-shape $input-entries) (tensor:inspect $input)
                  (let $input-coordinates (map-atom $input-entries $entry (car-atom $entry))
                    (and $valid
                      (and (tensor:same-index-sequence $shape $input-shape)
                           (tensor:same-index-sequence $coordinates $input-coordinates))))))))
         (if $same
           (let $mapped (map-atom $coordinates $coordinate
             (let* (($arguments (map-atom $inputs $input
                       (let (tensor:present $value) (tensor:at $input $coordinate) $value)))
                    ($all-arguments (union-atom $captures $arguments))
                    ($call (cons-atom $projection $all-arguments))
                    ($result (eval $call)))
               ($coordinate $result)))
             (tensor:from-coo $dtype $shape $mapped reject))
           (Tensor:Fault shape-mismatch))))))

(= (tensor:sum-reference $tensor)
   (let (TensorData $dtype $shape $entries) (tensor:inspect $tensor)
     (foldl-atom $entries (if (or (== $dtype f64) (== $dtype f32)) 0.0 0)
       $total $entry (+ $total (index-atom $entry 1)))))
(= (tensor:sum-by $tensor $projection)
   (let (TensorData $dtype $shape $entries) (tensor:inspect $tensor)
     (foldl-atom $entries 0 $total $entry
       (+ $total ($projection (index-atom $entry 1))))))
(= (tensor:map-stored $tensor $projection)
   (let (TensorData $dtype $shape $entries) (tensor:inspect $tensor)
     (let $mapped (map-atom $entries $entry
       (let ($coordinate $value) $entry ($coordinate ($projection $value))))
       (tensor:from-coo $dtype $shape $mapped reject))))

; Arbitrary duplicate combination is authored computation, not a native
; callback whitelist. Each occurrence is validated/coerced to the named
; dtype before combination; every combined result crosses that boundary too.
; The fold follows actual input order and does not assume commutativity,
; purity or a single answer. Only the final canonical constructor publishes
; a tensor. Inspection here is an explicit reference/fallback path.
(= (tensor:from-coo $dtype $shape $entries (combine $projection))
   (let $combined
     (tensor:combine-fold $dtype $shape $entries (Tensor:CombineRows ()) $projection)
     (case $combined
       (((Tensor:CombineRows $rows) (tensor:from-coo $dtype $shape $rows reject))
        ($failure $failure)))))
(= (tensor:combine-fold $dtype $shape $entries $state $projection)
   (if (== $entries ()) $state
     (let $next (tensor:combine-step $dtype $shape $state (car-atom $entries) $projection)
       (case $next
         (((Tensor:CombineRows $rows)
            (tensor:combine-fold $dtype $shape (cdr-atom $entries) $next $projection))
          ($failure $failure))))))
(= (tensor:combine-step $dtype $shape $state $entry $projection)
   (case $state
     (((Tensor:CombineRows $rows)
        (case (tensor:from-coo $dtype $shape ($entry) reject)
          (((Tensor:Fault $reason) (Tensor:Fault $reason))
           ($value
             (let (TensorData $actual-dtype $actual-shape (($coordinate $scalar)))
               (tensor:inspect $value)
               (tensor:combine-rows $dtype $shape $rows $coordinate $scalar $projection))))))
      ($failure $failure))))
(= (tensor:combine-rows $dtype $shape $rows $coordinate $scalar $projection)
   (if (== $rows ()) (Tensor:CombineRows (($coordinate $scalar)))
     (let* (($first (car-atom $rows)) (($existing $value) $first))
       (if (== $coordinate $existing)
         (let $merged ($projection $value $scalar)
           (case (tensor:from-coo $dtype $shape (($coordinate $merged)) reject)
             (((Tensor:Fault $reason) (Tensor:Fault $reason))
              ($tensor
                (let (TensorData $actual-dtype $actual-shape ($entry)) (tensor:inspect $tensor)
                  (let $tail (cdr-atom $rows)
                    (let $updated (cons-atom $entry $tail)
                      (Tensor:CombineRows $updated))))))))
         (case (tensor:combine-rows $dtype $shape (cdr-atom $rows) $coordinate $scalar $projection)
           (((Tensor:CombineRows $rest)
              (let $updated (cons-atom $first $rest) (Tensor:CombineRows $updated)))
            ($failure $failure)))))))

; The owned builder records occurrences in order. Explicitly freezing its
; log seals every alias before an arbitrary authored callback executes.
(= (tensor:builder $dtype $shape (combine $projection))
   (case (tensor:builder $dtype $shape first)
     (((Tensor:Fault $reason) (Tensor:Fault $reason))
      ($owner (Tensor:CombineBuilder $owner $projection)))))
(= (tensor:put (Tensor:CombineBuilder $owner $projection) $coordinate $value)
   (tensor:put $owner $coordinate $value))
(= (tensor:freeze (Tensor:CombineBuilder $owner $projection))
   (case (tensor:freeze-log $owner)
     (((TensorLog $dtype $shape $entries)
        (tensor:from-coo $dtype $shape $entries (combine $projection)))
      ($failure $failure))))
(= (tensor:close (Tensor:CombineBuilder $owner $projection)) (tensor:close $owner))

; Typed-buffer construction has the same authored callback path. Expanding
; its scalars is necessary only when a general callback was requested.
(= (tensor:from-coordinates $shape $coordinates $values (combine $projection))
   (tensor:coordinates-combine $shape $coordinates $values $projection))
(= (tensor:coordinates-combine $shape $coordinates $values $projection)
   (case ((tensor:inspect $coordinates) (tensor:inspect $values))
     ((((TensorData index ($n) $indices) (TensorData $dtype ($m) $scalars))
        (if (and (== $n $m) (and (== $n (size-atom $indices)) (== $n (size-atom $scalars))))
          (let $entries (map-atom $indices $entry
            (let* ((($ordinal $coordinate) $entry)
                   ($pair (index-atom $scalars $ordinal))
                   ($value (index-atom $pair 1)))
              ($coordinate $value)))
            (tensor:from-coo $dtype $shape $entries (combine $projection)))
          (Tensor:Fault shape-mismatch)))
      ($invalid (Tensor:Fault shape-mismatch)))))
