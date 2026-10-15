; Execute this example with -e, rather than a file argument. Without an entry
; file the getter reports missing context; cwd is not a fabricated entry path.
!(import! &self system)
!(system:entry-directory)
