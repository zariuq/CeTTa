!(import! &self langdef)
!(match &self (: langdef:quote-syntax $type) $type)
!(langdef:quote-syntax (+ 1 2))
