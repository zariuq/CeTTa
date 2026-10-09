!(import! &self system)
!(import! &self space_service)
!(let $socket (system:getenv-or-default "CETTA_SPACE_SOCKET" "")
   (space:open $socket "example" native))
