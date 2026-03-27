!(import! &self fs)

!(assertEqual (fs-exists? "tests/support/fs_fixture.txt") True)
!(assertEqual (fs-read-text "tests/support/fs_fixture.txt") "first line\nsecond line\n\nthird line\n")
!(assertEqual (fs-read-lines "tests/support/fs_fixture.txt") ("first line" "second line" "" "third line"))
!(assertEqual (fs-write-text "runtime/test_fs_surface_output.txt" "alpha") ())
!(assertEqual (fs-append-text "runtime/test_fs_surface_output.txt" "\nbeta") ())
!(assertEqual (fs-read-text "runtime/test_fs_surface_output.txt") "alpha\nbeta")
!(assertEqual (fs-read-lines "runtime/test_fs_surface_output.txt") ("alpha" "beta"))
