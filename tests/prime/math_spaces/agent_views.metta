!(import! &self math_space_agent)
!(bind! &views (math:new "views"))
!(math:goal &views "first" type (quote (u 0)) "A first open goal.")
!(math:goal &views "second" type (quote (u 0)) "A second open goal.")
!(math:goal &views "third" type (quote (u 0)) "A third open goal.")
!(bind! &cursor
   (case (math:view &views "goals" start 1 20)
     (((Math:Page $section $key $count $offset $items $next) $next))))
!(assertEqual
   (case (math:view &views "goals" start 1 20)
     (((Math:Page $section $key 3 0 ((Math:Text $digest $length clipped $text)) $next)
        (str:char-length $text))
      ($other Wrong))) 20)
!(assertEqual
   (case (math:view &views "goals" &cursor 1 300)
     (((Math:Page $section $key 3 1 ((Math:Text $digest $length complete $text)) $next) PageTwo)
      ($other Wrong))) PageTwo)
!(math:note &views "change" "A changed research context." ())
!(assertEqual (math:view &views "goals" &cursor 1 300)
   (Math:Stale cursor view-changed))
!(assertEqual (math:view &views "goals" start 0 300) (Math:Refused invalid-view-arguments))
!(assertEqual (math:view &views "goals" start 1.5 300) (Math:Refused invalid-view-arguments))
!(assertEqual (math:view &views "other" start 1 300) (Math:Refused invalid-view-arguments))
!(assertEqual (math:view &views "goals" start 1 -1) (Math:Refused invalid-view-arguments))
!(bind! &controller (new-state &views))
!(assertEqual
   (math:request &controller (json:parse "{\"id\":\"bad\",\"command\":\"check\",\"candidate\":\"c\",\"budget\":-1}"))
   (Math:Refused request-schema))
!(assertEqual
   (math:request &controller (json:parse "{\"id\":\"bad\",\"command\":\"summary\",\"extra\":true}"))
   (Math:Refused request-schema))
!(assertEqual
   (math:request &controller (json:parse "{\"id\":\"bad\",\"command\":\"summary\",\"id\":\"changed\"}"))
   (Math:Refused request-json))
!(assertEqual (math:request &controller (json:parse "[]")) (Math:Refused request-json))
!(assertEqual
   (math:request &controller (json:parse "{\"id\":\"bad\",\"command\":\"eval\"}"))
   (Math:Refused unknown-command))
!(assertEqual (math:request-field? symbol "(system:exit 1)") False)
!(assertEqual (math:request-field? symbol "scope:lemma") True)

; Reading and running an unlicensed host call cannot perform its effect.
!(bind! &view-counter (new-state 0))
!(chain (math:run &views (quote (change-state! &view-counter 1))) $unhandled
   (assertEqual (get-state &view-counter) 0))
