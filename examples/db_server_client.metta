; Run against native_db_server.metta with CETTA_DB_SOCKET selecting its socket.
!(import! &self (library db))
!(import! &self (library system))
!(db:connect claims (system:getenv-or-default "CETTA_DB_SOCKET" ""))
!(db:transaction claims (|-> ($space) (add-atom $space (edge alice bob))))
!(match (db:snapshot claims) (edge alice $who) $who)
!(db:info claims)
!(db:maintenance-info claims)
!(db:close claims)
