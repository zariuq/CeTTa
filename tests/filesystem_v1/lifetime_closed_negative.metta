!(import! &self fs)
!(import! &self system)
!(let $owner (fs:open-read (system:arg 0))
  (let $closed (fs:close $owner) (fs:read-chunk $owner 1)))
