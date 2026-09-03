!(import! &self fs)

(= (test:canonical-path-result (FS:CanonicalPath $_path)) True)

(= (test:canonical-path-fault (FS:CanonicalPathFault $_message)) True)

!(assertEqual (fs:exists "tests/support/fs_fixture.txt") True)
!(assertEqual
   (fs:canonical-path "./tests/support/../support/fs_fixture.txt")
   (fs:canonical-path "tests/support/fs_fixture.txt"))
!(assertEqual
   (fs:exists (fs:canonical-path "tests/support/fs_fixture.txt"))
   True)
!(assertEqual
   (fs:exists "tests/support/definitely_missing_fixture.txt")
   False)
!(assertEqual
   (test:canonical-path-result
     (fs:try-canonical-path "tests/support/fs_fixture.txt"))
   True)
!(assertEqual
   (test:canonical-path-fault
     (fs:try-canonical-path
       "tests/support/definitely_missing_fixture.txt"))
   True)
!(assertEqual
   (fs:try-canonical-from-file
     "tests/support/placeholder.metta" "fs_fixture.txt")
   (fs:try-canonical-path "tests/support/fs_fixture.txt"))
!(assertEqual
   (fs:try-canonical-under "tests" "support/fs_fixture.txt")
   (fs:try-canonical-path "tests/support/fs_fixture.txt"))
!(assertEqual
   (test:canonical-path-fault
     (fs:try-canonical-under
       "tests" "support/definitely_missing_fixture.txt"))
   True)
!(assertEqual
   (test:canonical-path-fault
     (fs:try-canonical-under
       "tests/support" "../test_fs_syntax.metta"))
   True)
!(assertEqual (fs:read-text "tests/support/fs_fixture.txt") "first line\nsecond line\n\nthird line\n")
!(assertEqual (fs:read-lines "tests/support/fs_fixture.txt") ("first line" "second line" "" "third line"))
!(assertEqual (fs:write "runtime/test_fs_syntax_output.txt" "alpha") ())
!(assertEqual (fs:append-text "runtime/test_fs_syntax_output.txt" "\nbeta") ())
!(assertEqual (fs:read-text "runtime/test_fs_syntax_output.txt") "alpha\nbeta")
!(assertEqual (fs:read-lines "runtime/test_fs_syntax_output.txt") ("alpha" "beta"))
