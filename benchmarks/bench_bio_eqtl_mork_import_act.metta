!(import! &self mork)
!(import! &self ../tests/support/bio_eqtl_mork_query_workload)

!(bind! &kb (mork:new-space))
!(mork:load-act! &kb "../runtime/bench_eqtl_for_mining.act")
!(run-bio-eqtl-mork-workload &kb)
