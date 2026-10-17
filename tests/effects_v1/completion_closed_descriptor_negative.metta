!(import! &self proc)
!(import! &self system)
; An internal process pipe must not turn a closed stdout into a valid output.
!(let $owner (proc:spawn ("/bin/sleep" "1") ())
  (let $written (system:write stdout "not-delivered\n")
    (proc:close $owner)))
