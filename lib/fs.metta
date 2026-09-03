(= (fs:read-text $path)
   (function
     (case (__cetta_lib_fs_read_text $path)
       (($result (return $result))))))

(= (fs:write $path $text)
   (function
     (case (__cetta_lib_fs_write $path $text)
       (($result (return $result))))))

(= (fs:append-text $path $text)
   (function
     (case (__cetta_lib_fs_append_text $path $text)
       (($result (return $result))))))

(= (fs:exists $path)
   (function
     (case (__cetta_lib_fs_exists $path)
       (($result (return $result))))))

(= (fs:canonical-path $path)
   (function
     (case (__cetta_lib_fs_canonical_path $path)
       (($result (return $result))))))

(= (fs:try-canonical-path $path)
   (function
     (case (__cetta_lib_fs_try_canonical_path $path)
       (($result (return $result))))))

; Resolve a relative path against the directory containing a base file, then
; canonicalize the existing target. Absolute requested paths are accepted
; unchanged. Failure remains data rather than an evaluator error.
(= (fs:try-canonical-from-file $base-file $requested-path)
   (function
     (case
       (__cetta_lib_fs_try_canonical_from_file $base-file $requested-path)
       (($result (return $result))))))

; Resolve a relative path beneath an explicit directory, then canonicalize
; the existing target. This operation performs no environment-variable or
; working-directory search.
(= (fs:try-canonical-under $directory $requested-path)
   (function
     (case (__cetta_lib_fs_try_canonical_under $directory $requested-path)
       (($result (return $result))))))

(= (fs:read-lines $path)
   (function
     (case (__cetta_lib_fs_read_lines $path)
       (($result (return $result))))))
