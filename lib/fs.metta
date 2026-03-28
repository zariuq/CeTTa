(= (fs-read-text $path)
   (function
     (case (__cetta_lib_fs_read_text $path)
       (($result (return $result))))))

(= (fs-write-text $path $text)
   (function
     (case (__cetta_lib_fs_write_text $path $text)
       (($result (return $result))))))

(= (fs-append-text $path $text)
   (function
     (case (__cetta_lib_fs_append_text $path $text)
       (($result (return $result))))))

(= (fs-exists? $path)
   (function
     (case (__cetta_lib_fs_exists $path)
       (($result (return $result))))))

(= (fs-read-lines $path)
   (function
     (case (__cetta_lib_fs_read_lines $path)
       (($result (return $result))))))
