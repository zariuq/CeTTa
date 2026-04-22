!(import! &self mork)
!(import! &self ../tests/support/bio_1m_mork_query_workload)

!(bind! &kb (new-space hash))
!(space-set-match-backend! &kb pathmap-imported)
!(mork:load-act! &kb "../runtime/bench_bio_1m.act")
!(run-bio-1m-mork-workload &kb)
