!(import! &self (library lib_memo))

(= (memo-library-square $x) (* $x $x))

!(memoize memo-library-square)
!(memo-library-square 9)
!(memo-library-square 9)
!(is-memoized memo-library-square 1)
!(get-memoize-stats)
