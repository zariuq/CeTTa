; SHA-256 is a pure opt-in primitive over exact MeTTa String bytes.
; These are NIST/FIPS 180-4 vectors also checked by the independent Lean path.
!(assertEqual
   (sha256 "")
   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
!(assertEqual
   (sha256 "abc")
   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
!(assertEqual
   (sha256 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
   "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")
!(assertEqualToResult
   (sha256 42)
   ((Error (sha256 42) (BadArgType 1 String Number))))
!(assertEqualToResult
   (sha256 "a" "b")
   ((Error (sha256 "a" "b") IncorrectNumberOfArguments)))
