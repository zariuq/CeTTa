!(import! &self fs)
!(import! &self cverify_textual_parse)

(= (mm-read-tokens $filename)
   (function
     (case (fs-read-text $filename)
       (((Error $source $message)
         (return (Error (mm-read-tokens $filename) $message)))
        ($text
         (case (mm-tokenize-text $text)
           ((parse-unsupported
             (return (Error (mm-read-tokens $filename) parse-unsupported)))
            ($tokens
             (return $tokens)))))))))

(= (mm-parse-file $filename)
   (function
     (case (parse_mm_file_textual $filename)
       (((Error $source $message)
         (return (Error (mm-parse-file $filename) $message)))
        ($result
         (return $result))))))

(= (mm-parse-file-pverify $filename)
   (function
     (case (parse_mm_file_textual $filename)
       (((Error $source $message)
         (return (Error (mm-parse-file-pverify $filename) $message)))
        ($result
         (return $result))))))
