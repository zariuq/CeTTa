!(import! &self proc)
!(import! &self str)
!(let $bytes (str:unescape metta "a\\x00b\\xff\\n")
  (proc:run ("/bin/cat") ((stdin $bytes))))
