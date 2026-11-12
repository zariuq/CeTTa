; Native mutation gate: argv execution, a bounded child lifetime, exact
; captures, and a specific abort witness. No Python or Prolog host is used.
!(import! &self proc)
!(import! &self system)
!(import! &self str)
!(import! &self fs)

(= (mutation:fail $message)
  (let $shown (system:write stderr (str:concat $message "\n"))
    (system:exit 1)))
(= (mutation:contains $text $needle)
  (case (str:find $text $needle)
    (((TextNotFound $missing) False)
     ($position (>= $position 0)))))
; A rejected run is reported with how it ended and what it wrote to stderr.
(= (mutation:fail-observed $message $observation)
  (case $observation
    (((proc:result $status $cause $out $err $capture)
      (mutation:fail (str:concat $message (str:concat ": "
        (str:concat (repr ($status $cause $capture)) (str:concat "\n" $err))))))
     ($other (mutation:fail (str:concat $message (str:concat ": " (repr $other))))))))

; Explicit selection avoids treating a fallback equation as an extra answer.
; The control and mutant cases below use case dispatch, not overload failure.
(= (mutation:control $observation $mutant $assertion)
  (case $observation
    (((proc:result (proc:exited 0) proc:normal $out $err
        (proc:capture $oc $oc $ec $ec proc:eof proc:eof (proc:input 0 0)))
      (if (mutation:contains $out "PASS:")
        (mutation:mutant (proc:run ($mutant) ((timeout-ms 180000))) $assertion)
        (mutation:fail "control did not report PASS:")))
     ($other (mutation:fail-observed "control failed or capture is incomplete" $other)))))
(= (mutation:mutant $observation $assertion)
  (case $observation
    (((proc:result (proc:signaled $signal) proc:normal $out $err
        (proc:capture $oc $oc $ec $ec proc:eof proc:eof (proc:input 0 0)))
      (if (and (== $signal (proc:abort-signal)) (mutation:contains $err $assertion))
        (system:write stdout
          (str:concat "PASS: native control accepted; mutation rejected by "
            (str:concat $assertion "\n")))
        (mutation:fail-observed "mutant aborted without the required assertion"
          $observation)))
     ($other (mutation:fail-observed "mutant did not abort with complete capture" $other)))))

!(if (== (system:argc) 3)
  (case (fs:try-canonical-path (system:arg 0))
    (((FS:CanonicalPath $control)
      (case (fs:try-canonical-path (system:arg 1))
        (((FS:CanonicalPath $mutant)
          (mutation:control (proc:run ($control) ((timeout-ms 180000)))
            $mutant (system:arg 2)))
         ((FS:CanonicalPathFault $reason) (mutation:fail $reason))
         ($unknown (mutation:fail "unexpected mutant path observation")))))
     ((FS:CanonicalPathFault $reason) (mutation:fail $reason))
     ($unknown (mutation:fail "unexpected control path observation"))))
  (let $usage (system:write stderr "usage: control mutant assertion\n") (system:exit 2)))
