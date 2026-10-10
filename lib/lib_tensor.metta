; Native compact CPU tensors. Inputs are preserved, including across alternatives.
; The C component owns storage and whole-buffer loops; composition lives here.
; TensorFailure values carry exact refusal reasons.

(: __cetta_lib_tensor_from (-> Atom Atom Atom Atom))
(: __cetta_lib_tensor_full (-> Atom Atom Atom Atom))
(: __cetta_lib_tensor_shape (-> Atom Atom))
(: __cetta_lib_tensor_dtype (-> Atom Atom))
(: __cetta_lib_tensor_to_expr (-> Atom Atom))
(: __cetta_lib_tensor_reshape (-> Atom Atom Atom))
(: __cetta_lib_tensor_copy (-> Atom Atom))
(: __cetta_lib_tensor_with (-> Atom Atom Atom Atom))
(: __cetta_lib_tensor_add (-> Atom Atom Atom))
(: __cetta_lib_tensor_mul (-> Atom Atom Atom))
(: __cetta_lib_tensor_sum (-> Atom Atom))
(: __cetta_lib_tensor_dot (-> Atom Atom Atom))
(: __cetta_lib_tensor_encode (-> Atom Atom))
(: __cetta_lib_tensor_decode (-> Atom Atom))
(: __cetta_lib_tensor_stack (-> Atom Atom Atom Atom))
(: __cetta_lib_tensor_row_norms (-> Atom Atom))
(: __cetta_lib_tensor_cosine_rows (-> Atom Atom Atom Atom))

(: tensor:stack (-> %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:stack $dtype $width $rows)
   (function (chain (eval (__cetta_lib_tensor_stack $dtype $width $rows)) $result (return $result))))
(: tensor:row-norms (-> %Undefined% Atom))
(= (tensor:row-norms $matrix)
   (function (chain (eval (__cetta_lib_tensor_row_norms $matrix)) $result (return $result))))
; Norms must correspond to the immutable matrix; kept separately to amortize
; their construction across queries. All buffers and dimensions are checked.
(: tensor:cosine-rows (-> %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:cosine-rows $matrix $norms $query)
   (function (chain (eval (__cetta_lib_tensor_cosine_rows $matrix $norms $query)) $result (return $result))))

(: tensor:from (-> %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:from $dtype $shape $values)
   (function
     (chain (eval (__cetta_lib_tensor_from $dtype $shape $values)) $result
       (return $result))))

(: tensor:full (-> %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:full $dtype $shape $value)
   (function
     (chain (eval (__cetta_lib_tensor_full $dtype $shape $value)) $result
       (return $result))))

(: tensor:shape (-> %Undefined% Atom))
(= (tensor:shape $tensor)
   (function
     (chain (eval (__cetta_lib_tensor_shape $tensor)) $result
       (return $result))))

(: tensor:dtype (-> %Undefined% Atom))
(= (tensor:dtype $tensor)
   (function
     (chain (eval (__cetta_lib_tensor_dtype $tensor)) $result
       (return $result))))

(: tensor:to-expr (-> %Undefined% Atom))
(= (tensor:to-expr $tensor)
   (function
     (chain (eval (__cetta_lib_tensor_to_expr $tensor)) $result
       (return $result))))

(: tensor:reshape (-> %Undefined% %Undefined% Atom))
(= (tensor:reshape $tensor $shape)
   (function
     (chain (eval (__cetta_lib_tensor_reshape $tensor $shape)) $result
       (return $result))))

(: tensor:copy (-> %Undefined% Atom))
(= (tensor:copy $tensor)
   (function
     (chain (eval (__cetta_lib_tensor_copy $tensor)) $result
       (return $result))))

(: tensor:with (-> %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:with $tensor $index $value)
   (function
     (chain (eval (__cetta_lib_tensor_with $tensor $index $value)) $result
       (return $result))))

(: tensor:add (-> %Undefined% %Undefined% Atom))
(= (tensor:add $left $right)
   (function
     (chain (eval (__cetta_lib_tensor_add $left $right)) $result
       (return $result))))

(: tensor:mul (-> %Undefined% %Undefined% Atom))
(= (tensor:mul $left $right)
   (function
     (chain (eval (__cetta_lib_tensor_mul $left $right)) $result
       (return $result))))

(: tensor:sum (-> %Undefined% Atom))
(= (tensor:sum $tensor)
   (function
     (chain (eval (__cetta_lib_tensor_sum $tensor)) $result
       (return $result))))

(: tensor:dot (-> %Undefined% %Undefined% Atom))
(= (tensor:dot $left $right)
   (function
     (chain (eval (__cetta_lib_tensor_dot $left $right)) $result
       (return $result))))

(: tensor:encode (-> %Undefined% Atom))
(= (tensor:encode $tensor)
   (function
     (chain (eval (__cetta_lib_tensor_encode $tensor)) $result
       (return $result))))

(: tensor:decode (-> %Undefined% Atom))
(= (tensor:decode $bytes)
   (function
     (chain (eval (__cetta_lib_tensor_decode $bytes)) $result
       (return $result))))

; Norm and cosine compose whole-buffer native operations.
(: tensor:norm (-> %Undefined% %Undefined%))
(= (tensor:norm $x)
   (case (tensor:dot $x $x)
     (((TensorFailure $reason) (TensorFailure $reason))
      ($value (sqrt-math $value)))))

(: tensor:cosine (-> %Undefined% %Undefined% %Undefined%))
(= (tensor:cosine $x $y)
   (case (tensor:norm $x)
     (((TensorFailure $reason) (TensorFailure $reason))
      ($nx (case (tensor:norm $y)
        (((TensorFailure $reason) (TensorFailure $reason))
         ($ny (if (or (== $nx 0) (== $ny 0))
           (TensorFailure zero-norm)
           (case (tensor:dot $x $y)
             (((TensorFailure $reason) (TensorFailure $reason))
              ($dot (/ $dot (* $nx $ny)))))))))))))

; Whole-buffer selection and arithmetic, usable by any tensor client.
(: __cetta_lib_tensor_gather (-> Atom Atom Atom))
(: tensor:gather (-> %Undefined% %Undefined% Atom))
(= (tensor:gather $tensor $indices)
   (function (chain (eval (__cetta_lib_tensor_gather $tensor $indices)) $result (return $result))))

(: __cetta_lib_tensor_matvec (-> Atom Atom Atom))
(: tensor:matvec (-> %Undefined% %Undefined% Atom))
(= (tensor:matvec $matrix $query)
   (function (chain (eval (__cetta_lib_tensor_matvec $matrix $query)) $result (return $result))))

(: __cetta_lib_tensor_matvec_selected (-> Atom Atom Atom Atom))
(: tensor:matvec-selected (-> %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:matvec-selected $matrix $query $indices)
   (function (chain (eval (__cetta_lib_tensor_matvec_selected $matrix $query $indices)) $result (return $result))))

(: __cetta_lib_tensor_matvec_provider (-> Atom Atom))
(: tensor:matvec-provider (-> Atom))
(= (tensor:matvec-provider)
   (function (chain (eval (__cetta_lib_tensor_matvec_provider auto)) $result (return $result))))

(: __cetta_lib_tensor_matvec_with (-> Atom Atom Atom Atom))
(: tensor:matvec-with (-> %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:matvec-with $provider $matrix $query)
   (function (chain (eval (__cetta_lib_tensor_matvec_with $provider $matrix $query)) $result (return $result))))

(: __cetta_lib_tensor_matvec_selected_with (-> Atom Atom Atom Atom Atom))
(: tensor:matvec-selected-with (-> %Undefined% %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:matvec-selected-with $provider $matrix $query $indices)
   (function (chain (eval (__cetta_lib_tensor_matvec_selected_with $provider $matrix $query $indices)) $result (return $result))))

(: __cetta_lib_tensor_div (-> Atom Atom Atom))
(: tensor:div (-> %Undefined% %Undefined% Atom))
(= (tensor:div $left $right)
   (function (chain (eval (__cetta_lib_tensor_div $left $right)) $result (return $result))))

(: __cetta_lib_tensor_scale (-> Atom Atom Atom))
(: tensor:scale (-> %Undefined% %Undefined% Atom))
(= (tensor:scale $tensor $factor)
   (function (chain (eval (__cetta_lib_tensor_scale $tensor $factor)) $result (return $result))))

(: __cetta_lib_tensor_mask_indices (-> Atom Atom Atom))
(: tensor:mask-indices (-> %Undefined% %Undefined% Atom))
(= (tensor:mask-indices $length $indices)
   (function (chain (eval (__cetta_lib_tensor_mask_indices $length $indices)) $result (return $result))))

(: __cetta_lib_tensor_mask_and (-> Atom Atom Atom))
(: tensor:mask-and (-> %Undefined% %Undefined% Atom))
(= (tensor:mask-and $left $right)
   (function (chain (eval (__cetta_lib_tensor_mask_and $left $right)) $result (return $result))))

(: __cetta_lib_tensor_mask_or (-> Atom Atom Atom))
(: tensor:mask-or (-> %Undefined% %Undefined% Atom))
(= (tensor:mask-or $left $right)
   (function (chain (eval (__cetta_lib_tensor_mask_or $left $right)) $result (return $result))))

(: __cetta_lib_tensor_mask_not (-> Atom Atom))
(: tensor:mask-not (-> %Undefined% Atom))
(= (tensor:mask-not $mask)
   (function (chain (eval (__cetta_lib_tensor_mask_not $mask)) $result (return $result))))

(: __cetta_lib_tensor_nonzero (-> Atom Atom))
(: tensor:nonzero (-> %Undefined% Atom))
(= (tensor:nonzero $mask)
   (function (chain (eval (__cetta_lib_tensor_nonzero $mask)) $result (return $result))))

(: __cetta_lib_tensor_top_k (-> Atom Atom Atom))
(: tensor:top-k (-> %Undefined% %Undefined% Atom))
(= (tensor:top-k $scores $k)
   (function (chain (eval (__cetta_lib_tensor_top_k $scores $k)) $result (return $result))))

(: __cetta_lib_tensor_top_k_masked (-> Atom Atom Atom Atom))
(: tensor:top-k-masked (-> %Undefined% %Undefined% %Undefined% Atom))
(= (tensor:top-k-masked $scores $mask $k)
   (function (chain (eval (__cetta_lib_tensor_top_k_masked $scores $mask $k)) $result (return $result))))

; Selectors and masks never require an Atom for each matrix element.
(= (tensor:indices $values) (tensor:from i64 ((size-atom $values)) $values))
; Row norms must belong to this immutable matrix and be positive and finite.
; The matrix kernel uses a fixed four-lane reduction; cosine stays composition.
(= (tensor:cosine-selected $matrix $norms $query $indices)
   (case (tensor:norm $query)
     (((TensorFailure $why) (TensorFailure $why))
      ($qn (if (or (<= $qn 0) (or (isnan-math $qn) (isinf-math $qn)))
        (TensorFailure invalid-value)
        (case (tensor:matvec-selected $matrix $query $indices)
          (((TensorFailure $why) (TensorFailure $why))
           ($dots (case (tensor:gather $norms $indices)
             (((TensorFailure $why) (TensorFailure $why))
              ($rn (case (tensor:div $dots $rn)
                (((TensorFailure $why) (TensorFailure $why))
                 ($ratios (tensor:scale $ratios (/ 1.0 $qn))))))))))))))))

(: __cetta_lib_tensor_div_rows (-> Atom Atom Atom))
(: tensor:div-rows (-> %Undefined% %Undefined% Atom))
(= (tensor:div-rows $matrix $divisors)
   (function (chain (eval (__cetta_lib_tensor_div_rows $matrix $divisors)) $result (return $result))))
(= (tensor:normalize-rows $matrix)
   (case (tensor:row-norms $matrix)
     (((TensorFailure $why) (TensorFailure $why))
      ($norms (tensor:div-rows $matrix $norms)))))
; The supplied matrix must have been normalized; inputs remain immutable.
(= (tensor:cosine-unit-rows $matrix $query $indices)
   (case (tensor:norm $query)
     (((TensorFailure $why) (TensorFailure $why))
      ($norm (if (or (<= $norm 0) (or (isnan-math $norm) (isinf-math $norm)))
        (TensorFailure invalid-value)
        (tensor:matvec-selected $matrix (tensor:scale $query (/ 1.0 $norm)) $indices))))))
