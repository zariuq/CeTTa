; Shell syntax is interpreted by the platform's sh, invoked by the native
; process library. The parent environment and working directory are inherited.
; Timeout is in seconds: 0 means no deadline. Output is merged into a temporary
; file, so background descendants cannot keep a finished shell call waiting on
; a pipe. Zero max-bytes preserves the complete output snapshot.
!(import! &self proc)
!(import! &self system)
!(import! &self math)

(= (shell:run $command $seconds)
   (shell:run $command $seconds 0 ""))
(= (shell:run $command $seconds $max)
   (shell:run $command $seconds $max ""))
(= (shell:run $command $seconds $max $progress)
   (if (>= $seconds 0)
     (proc:run ("sh" "-c" $command) (system:cwd) inherit
             (math:ceil (* $seconds 1000)) $max
             (proc:options "merged-file" $progress))
     (proc:failed "timeout seconds must be nonnegative")))
