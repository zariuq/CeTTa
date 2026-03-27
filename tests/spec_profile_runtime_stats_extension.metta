!(bind! &stats (runtime-stats!))
!(match &stats (runtime-counter bindings-lookup $n) $n)
!(reset-runtime-stats!)
