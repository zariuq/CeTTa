(= (mm-read-tokens $filename)
   (function
     (case (__cetta_lib_metamath_read_tokens $filename)
       (($result (return $result))))))

(= (mm-open-file $filename)
   (function
     (case (__cetta_lib_metamath_open_file $filename)
       (($result (return $result))))))

(= (mm-open-file-pverify $filename)
   (function
     (case (__cetta_lib_metamath_open_file_pverify $filename)
       (($result (return $result))))))

(= (mm-next-stmt $handle)
   (function
     (case (__cetta_lib_metamath_next_stmt $handle)
       (($result (return $result))))))

(= (mm-close-file $handle)
   (function
     (case (__cetta_lib_metamath_close_file $handle)
       (($result (return $result))))))

(= (mm-parse-file $filename)
   (function
     (case (__cetta_lib_metamath_parse_file $filename)
       (($result (return $result))))))

(= (mm-parse-file-pverify $filename)
   (function
     (case (__cetta_lib_metamath_parse_file_pverify $filename)
       (($result (return $result))))))
