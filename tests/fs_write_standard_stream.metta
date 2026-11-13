; fs:write and fs:append-text to a standard stream's device path write through
; the stream, in order with what the program prints, also when stdout is a
; redirected regular file (reopening the path would truncate it).
!(import! &self fs)
!(println! before)
!(fs:write "/dev/stdout" "written\n")
!(fs:append-text "/dev/stdout" "appended\n")
!(fs:write "/dev/stderr" "to-err\n")
!(println! after)
