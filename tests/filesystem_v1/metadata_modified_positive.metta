!(import! &self fs)
!(import! &self system)
!(let $plain (fs:metadata (system:arg 0) follow)
  (let $timed (fs:metadata (system:arg 0) follow-modified) (PlainAndTimed $plain $timed)))
