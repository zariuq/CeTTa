(= (fs:read-text $path)
   (function
     (case (__cetta_lib_fs_read_text $path)
       (($result (return $result))))))

; Structured results: ("ok" text), ("limit" max-bytes), ("missing" cwd),
; ("directory" ""), ("denied" ""), or ("error" class detail).
; 0 means unlimited. Raw preserves bytes; replace repairs malformed UTF-8.
(= (fs:read-text-result $path $max-bytes $errors)
   (function
     (case (__cetta_lib_fs_read_text_result $path $max-bytes $errors)
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

; The complete lines within bytes [start, end) of a file (end -1: its end).
; A range starting inside the file begins at the next line.
(= (fs:read-lines-between $path $start $end)
   (function
     (case (__cetta_lib_fs_read_lines_between $path $start $end)
       (($result (return $result))))))

; Streaming numbered window: ("window" start count more numbered-text empty).
; Tree page: ("page" text more next-offset) or ("not-directory" path).
; Grep page: ("matches" text more next-offset files-scanned errors).
; Page sizes, offsets, recursion depth and skipped names are caller choices.
; Read errors are ("error" detail). Search uses PCRE2 when available.
; Update: ("written" absolute-path bytes sha256-prefix backup-path),
; ("count" occurrences path) for a non-unique edit, or ("error" detail).
; Updates preserve bytes on append, normalize newlines on a strict UTF-8 edit,
; and atomically replace with a collision-free backup in the supplied directory.
(= (fs:line-window $path $start $count)
   (function
     (case (__cetta_lib_fs_line_window $path $start $count)
       (($result (return $result))))))

(= (fs:tree-page $path $depth $limit $offset $skips)
   (function
     (case (__cetta_lib_fs_tree_page $path $depth $limit $offset $skips)
       (($result (return $result))))))

(= (fs:grep-page $pattern $path $limit $offset $skips)
   (function
     (case (__cetta_lib_fs_grep_page $pattern $path $limit $offset $skips)
       (($result (return $result))))))

(= (fs:update-text $path $operation $first $second $backup-directory)
   (function
     (case (__cetta_lib_fs_update_text $path $operation $first $second $backup-directory)
       (($result (return $result))))))

(= (fs:is-file $path)
   (function
     (case (__cetta_lib_fs_is_file $path)
       (($result (return $result))))))
