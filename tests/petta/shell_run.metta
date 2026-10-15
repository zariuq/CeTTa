; No Python or Prolog imports: shell I/O comes from the native process library.
!(import! &self (library shell))
!(shell:run "printf 'λ 🌸'; printf ' error' >&2" 1)
!(shell:run "exit 7" 0)
!(shell:run "printf before; sleep 2; printf after" 0.05)
!(shell:run "printf 1234567890" 1 5)
!(shell:run "printf should-not-run" -0.000001)
!(shell:run "printf done" 0)
