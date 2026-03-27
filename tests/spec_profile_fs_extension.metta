!(import! &self fs)

!(assertEqual (fs-exists? "tests/support/fs_fixture.txt") True)
!(assertEqual (fs-read-lines "tests/support/fs_fixture.txt") ("first line" "second line" "" "third line"))
