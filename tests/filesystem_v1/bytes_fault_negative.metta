!(import! &self fs)
!(import! &self system)
; Opening a directory as a byte reader is allowed; the attempted read reports
; the OS fault rather than manufacturing an end observation.
!(let $owner (fs:open-read (system:arg 0))
  (let $fault (fs:read-chunk $owner 1)
    (let $closed (fs:close $owner) (BytesFault $fault))))
