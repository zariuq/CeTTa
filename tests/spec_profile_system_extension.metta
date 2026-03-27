!(import! &self system)

!(assertEqual (get-type (system-cwd)) String)
!(assertEqual (system-has-args) False)
