; A path is a string. Anything else is refused at once with an error that
; names the call, instead of being taken apart without end.
!(import! &self (library path))
!(path:parts 42)
!(path:dirname (Error x y))
!(path:join "a" 7)
!(path:resolve-from 1 "a")
!(path:dirname "/a/b/file.mm")
