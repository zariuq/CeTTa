!(import! &self proc)
!(let $owner (proc:spawn ("/bin/echo" "cancelled") ())
  (let $closed (proc:close $owner) (proc:poll $owner)))
