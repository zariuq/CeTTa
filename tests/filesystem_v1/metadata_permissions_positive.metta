!(import! &self fs)
!(import! &self system)
!(let $followed (fs:metadata (system:arg 0) follow-permissions)
  (let $link (fs:metadata (system:arg 0) no-follow-permissions) (FollowedAndLink $followed $link)))
