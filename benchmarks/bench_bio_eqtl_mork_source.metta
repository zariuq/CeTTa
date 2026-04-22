!(import! &self mork)
!(import! &self ../tests/support/bio_eqtl_mork_query_workload)

!(import! &src ../tests/eqtl_for_mining)
!(space-set-match-backend! &src pathmap-imported)
!(bind! &kb (mork:new-space))
!(match &src $atom (mork:add-atom &kb $atom))
!(run-bio-eqtl-mork-workload &kb)
