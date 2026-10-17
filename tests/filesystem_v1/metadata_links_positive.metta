!(import! &self fs)
!(import! &self system)
!(let $link (fs:metadata (system:arg 0) no-follow)
  (let $target (fs:metadata (system:arg 0) follow) (LinkAndTarget $link $target)))
