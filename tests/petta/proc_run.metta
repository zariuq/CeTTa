; lib/proc: a program run as a child process with an explicit argument
; vector, working directory and environment, bounded by a timeout and an
; output cap. Nothing of this process's environment or open files reaches
; the program; a timeout kills its whole process group.
!(import! &self (library proc))
!(proc:run ("/bin/echo" "hello" "world") "/" () 5000 1000)
!(proc:run ("echo" "found") "/" (("PATH" "/usr/bin:/bin")) 5000 1000)
!(proc:run ("echo" "unfound") "/" () 5000 1000)
!(proc:run ("/usr/bin/env") "/" (("ONLY" "this")) 5000 1000)
!(proc:run ("/bin/sh" "-c" "echo out; echo err >&2; exit 3") "/" () 5000 1000)
!(proc:run ("/bin/pwd") "/" () 5000 1000)
!(proc:run ("/bin/sh" "-c" "i=0; while [ $i -lt 2000 ]; do printf aaaaaaaaaa; i=$((i+1)); done") "/" () 5000 10)
!(proc:run ("/bin/sh" "-c" "sleep 31.25 & echo started") "/" () 300 1000)
!(proc:run ("/bin/sh" "-c" "kill -9 $$") "/" () 5000 100)
!(proc:run ("/bin/pwd") "/nonexistent-directory" () 5000 100)
!(proc:run ("/nonexistent-program") "/" () 5000 100)
!(proc:run ("/bin/sh" "-c" "printf 'a\\377b\\000c'") "/" () 5000 100)
!(proc:run ("/bin/ls" "/proc/self/fd") "/" () 5000 1000)
!(proc:run () "/" () 5000 100)
!(proc:run ("/bin/echo") "/" (("A=B" "c")) 5000 100)
