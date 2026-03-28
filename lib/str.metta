(= (str-length $text)
   (function
     (case (__cetta_lib_str_length $text)
       (($result (return $result))))))

(= (str-concat $lhs $rhs)
   (function
     (case (__cetta_lib_str_concat $lhs $rhs)
       (($result (return $result))))))

(= (str-split $separator $text)
   (function
     (case (__cetta_lib_str_split $separator $text)
       (($result (return $result))))))

(= (str-split-whitespace $text)
   (function
     (case (__cetta_lib_str_split_whitespace $text)
       (($result (return $result))))))

(= (str-join $separator $parts)
   (function
     (case (__cetta_lib_str_join $separator $parts)
       (($result (return $result))))))

(= (str-slice $text $start $end)
   (function
     (case (__cetta_lib_str_slice $text $start $end)
       (($result (return $result))))))

(= (str-find $text $needle)
   (function
     (case (__cetta_lib_str_find $text $needle)
       (($result (return $result))))))

(= (str-starts-with? $text $prefix)
   (function
     (case (__cetta_lib_str_starts_with $text $prefix)
       (($result (return $result))))))

(= (str-ends-with? $text $suffix)
   (function
     (case (__cetta_lib_str_ends_with $text $suffix)
       (($result (return $result))))))

(= (str-trim $text)
   (function
     (case (__cetta_lib_str_trim $text)
       (($result (return $result))))))
