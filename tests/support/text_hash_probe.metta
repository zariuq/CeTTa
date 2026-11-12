!(import! &self (library str))
(= (text-hash:probe (Hash $initial $records)) (str:sha256-chain $initial $records))
(= (text-hash:probe (Strip $text $characters)) (str:strip-python $text $characters))
