!(import! &self leaf)
!(match &self (edge $a $b) (match &self (edge $b $c) (path $a $b $c)))
