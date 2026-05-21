; Strict-core rho-calculus helper surface.
; Canonical terms use the rho: namespace. Dotted rho. spelling is reader
; sugar for the same symbols.

(: rho:step (-> Atom Atom))

(@doc rho:step
  (@desc "Runs one strict rho-calculus COMM step over a rho:* process and emits zero or more one-step successor processes as ordinary MeTTa results. The input is elaborated with the same binder discipline as --lang rhocalc/mrho before validation and reduction. (pragma! rho-step-threads N) enables deterministic OS-threaded COMM frontier enumeration when N is greater than 1; core COMM cases that would require capture-avoidance alpha-renaming use the sequential path.")
  (@params (
    (@param "Strict-core rho process built from rho:nil, rho:par, rho:send, rho:recv, rho:quote, and rho:drop")))
  (@return "Zero or more one-step successor rho processes via ordinary MeTTa nondeterminism"))
