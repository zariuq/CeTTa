; lib_memo's answers follow function definitions (the observable cases of
; stable-fixed's lib_memo.pl since 94e7352, from its definition-order tests).
!(import! &self (library lib_memo))

; A caller translated before memoize starts using the memo table: the
; memoized body runs once for two calls.
!(bind! do_ticks (new-state 0))
(= (do_slow $x) (let $ignored (change-state! do_ticks (+ (get-state do_ticks) 1)) (+ $x 1)))
(= (do_memo_reader $x) (do_slow $x))
!(memoize do_slow)
!(do_memo_reader 5)
!(do_memo_reader 5)
!(get-state do_ticks)

; A cached data answer is dropped when its head gains an equation.
(= (do_cached_reader $x) (do_cached_late $x))
!(memoize do_cached_reader)
!(do_cached_reader 5)
!(add-atom &self (= (do_cached_late $x) (+ $x 1)))
!(do_cached_reader 5)

; The dependency runs through a caller that is not memoized.
(= (do_cache_top $x) (do_cache_mid $x))
(= (do_cache_mid $x) (do_cache_leaf $x))
!(memoize do_cache_top)
!(do_cache_top 5)
!(add-atom &self (= (do_cache_leaf $x) (* $x 10)))
!(do_cache_top 5)

; An equation that arrives and then retires is followed both ways.
(= (do_cache_wait_top $x) (do_cache_wait_mid $x))
(= (do_cache_wait_mid $x) (do_cache_wait_leaf $x))
!(memoize do_cache_wait_top)
!(do_cache_wait_top 5)
(= (do_cache_wait_leaf $x) (* $x 10))
!(do_cache_wait_top 5)
!(remove-atom &self (= (do_cache_wait_leaf $x) (* $x 10)))
!(do_cache_wait_top 5)
