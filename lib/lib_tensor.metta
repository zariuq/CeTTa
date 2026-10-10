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
