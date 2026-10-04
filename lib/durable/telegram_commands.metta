!(import! &self str)
!(import! &self json)
!(import! &self durable:telegram)

; Operator commands of telegram-channel/1. A deployment declares them as
; data, which the host loads from a file of declarations only:
;   (tg-cmd:command NAME KIND HELP)
;   (tg-cmd:agent NAME)      optional: how replies name the agent
;   (tg-cmd:bot USERNAME)    optional: accept /command@USERNAME
; NAME is "/" and 1..32 of a-z, 0-9 and "_". KIND is one of
;   help       the service lists the declared commands;
;   stop       the service stops dispatching the agent's sends, answers at
;              once and tells the agent;
;   start      the service resumes dispatch, answers at once and tells the
;              agent;
;   delegated  the agent answers. The service owns a deadline: if the agent
;              has not answered by then, the service replies with its own
;              notice and edits it when the answer arrives.
; An operator's message whose text is a declared command, optionally with
; arguments, is a command. Anything else is ordinary input.

; The message text as a plain string. A command is printable text without
; quotes or backslashes, so the JSON literal of such a text, without its
; quotes, is the text itself; a text needing any escape is not a command.
(= (tg-cmd:plain $json-string)
   (let $literal (json:stringify $json-string)
     (case (str:find $literal "\\")
       (((TextNotFound $needle)
         (tg:some (str:byte-slice $literal 1 (- (str:byte-length $literal) 1))))
        ($found tg:none)))))

(= (tg-cmd:text $update)
   (case (telegram:field $update "message")
     (((tg:some $message)
       (case (telegram:field $message "text")
         (((tg:some (JsonStringV1 $scalars)) (tg-cmd:plain (JsonStringV1 $scalars)))
          ($other tg:none))))
      ($other tg:none))))

(= (tg-cmd:kind $name)
   (case (collapse (match &self (tg-cmd:command $name $kind $help) $kind))
     ((($kind) (tg:some $kind))
      ($other tg:none))))

; /name or /name@bot, when bot is the declared username.
(= (tg-cmd:addressee $head)
   (case (str:split "@" $head)
     ((($name) (tg:some $name))
      (($name $bot)
       (if (== (collapse (match &self (tg-cmd:bot $bot) True)) (True)) (tg:some $name) tg:none))
      ($other tg:none))))

(= (tg-cmd:parse $text)
   (let $words (str:split-whitespace $text)
     (if (== $words ()) tg:none
       (let $head (car-atom $words)
         (let $rest (cdr-atom $words)
           (if (not (str:starts-with $head "/")) tg:none
             (case (tg-cmd:addressee $head)
               (((tg:some $name)
                 (case (tg-cmd:kind $name)
                   (((tg:some $kind) (tg-cmd:call $name $kind (str:join " " $rest)))
                    ($unknown tg:none))))
                ($other tg:none)))))))))

; The command an update carries, if its sender is an operator.
(= (tg-cmd:of $kind $role $update)
   (if (and (== $kind "message") (== $role operator))
     (case (tg-cmd:text $update)
       (((tg:some $text) (tg-cmd:parse $text))
        ($none tg:none)))
     tg:none))

(= (tg-cmd:agent-name)
   (case (collapse (match &self (tg-cmd:agent $name) $name))
     ((($name) $name)
      ($other "the agent"))))

(= (tg-cmd:named $kind)
   (case (collapse (match &self (tg-cmd:command $name $kind $help) $name))
     ((($name) $name)
      ($other ""))))

(= (tg-cmd:help-text)
   (str:join "\n" (collapse (match &self (tg-cmd:command $name $kind $help)
                              (format-args "{} — {}" ($name $help))))))

(= (tg-cmd:stopped-text)
   (let $agent (tg-cmd:agent-name)
     (let $start (tg-cmd:named start)
       (format-args "Stopped. Nothing {} submits is sent until {}." ($agent $start)))))

(= (tg-cmd:started-text)
   (let $agent (tg-cmd:agent-name)
     (format-args "Started. {}'s messages go out again." ($agent))))

(= (tg-cmd:fallback-text)
   (let $agent (tg-cmd:agent-name)
     (format-args "No answer from {} yet. This message will be updated when it arrives." ($agent))))

; The agent's answer: ["answer", text]. Longer text is cut to fit one
; message, in Unicode scalars so no character is split; an empty answer is
; said so. Anything else is no answer.
(= (tg-cmd:answer-text $body)
   (case (json-parse $body)
     (((JsonArray ((JsonString "answer") (JsonString $text)))
       (let $length (str:char-length $text)
         (if (== $length 0) (tg:some "(empty answer)")
           (if (> $length 3800) (tg:some (str:char-slice $text 0 3800)) (tg:some $text)))))
      ($other tg:none))))

; The agent's task for a command: ["command", key, name, arguments, lane].
; Names are declared; arguments contain no quote, backslash or control
; character; the lane is CHAT.THREAD of the operator's message.
(= (tg-cmd:observation $key $name $args $lane)
   (format-args "[\"command\",\"{}\",\"{}\",\"{}\",\"{}\"]" ($key $name $args $lane)))

; The lifecycle latch, read from the given view.
(= (tg-cmd:stopped $view)
   (== (collapse (tg-host:match (host:record $view "latch" (tg-cmd:latch stopped)) True)) (True)))

; Button menus. A delegated command, or a tap on one of the buttons, may be
; answered with a menu: ["menu", toast, text, rows], rows being
; [[[label, data], ...], ...]. The service sends or edits the text with the
; buttons, and a tap's toast is its answer to the tap. A label is 1..64
; scalars and data 1..64 bytes, neither needing a JSON escape; at most 100
; rows and 100 buttons, 8 to a row. Empty rows mean no buttons. A menu that
; breaks any of these is no answer.
(= (tg-cmd:clean $text $scalars $bytes)
   (case (str:find (json-stringify (JsonString $text)) "\\")
     (((TextNotFound $needle)
       (and (> (str:byte-length $text) 0)
            (and (<= (str:char-length $text) $scalars) (<= (str:byte-length $text) $bytes))))
      ($found False))))

(= (tg-cmd:button $pair)
   (case $pair
     (((JsonArray ((JsonString $label) (JsonString $data)))
       (if (and (tg-cmd:clean $label 64 256) (tg-cmd:clean $data 64 64))
         (tg:some (telegram:button $label $data)) tg:none))
      ($other tg:none))))

; A row's buttons, and what remains of the budget of buttons.
(= (tg-cmd:row $pairs $budget)
   (if (== $pairs ()) (tg:row () $budget)
     (if (<= $budget 0) tg:none
       (let $head (car-atom $pairs)
         (let $tail (cdr-atom $pairs)
           (case (tg-cmd:button $head)
             (((tg:some $button)
               (case (tg-cmd:row $tail (- $budget 1))
                 (((tg:row $rest $left)
                   (let $built (cons-atom $button $rest) (tg:row $built $left)))
                  ($bad tg:none))))
              ($bad tg:none))))))))

(= (tg-cmd:rows $rows $budget $count)
   (if (== $rows ()) (tg:some ())
     (if (>= $count 100) tg:none
       (let $head (car-atom $rows)
         (let $tail (cdr-atom $rows)
           (case $head
             (((JsonArray $pairs)
               (if (or (== $pairs ()) (> (size-atom $pairs) 8)) tg:none
                 (case (tg-cmd:row $pairs $budget)
                   (((tg:row $row $left)
                     (case (tg-cmd:rows $tail $left (+ $count 1))
                       (((tg:some $rest)
                         (let $built (cons-atom $row $rest) (tg:some $built)))
                        ($bad tg:none))))
                    ($bad tg:none)))))
              ($bad tg:none))))))))

(= (tg-cmd:keyboard $rows)
   (case (tg-cmd:rows $rows 100 0)
     (((tg:some ()) (tg:some ()))
      ((tg:some $built) (tg:some (telegram:keyboard $built)))
      ($bad tg:none))))

(= (tg-cmd:message-text $text)
   (let $length (str:char-length $text)
     (if (== $length 0) "(empty answer)"
       (if (> $length 3800) (str:char-slice $text 0 3800) $text))))

(= (tg-cmd:toast $text)
   (if (> (str:char-length $text) 200) (str:char-slice $text 0 200) $text))

; An answer or a menu: (tg-cmd:reply toast text keyboard), keyboard () for none.
(= (tg-cmd:reply $body)
   (case (json-parse $body)
     (((JsonArray ((JsonString "answer") (JsonString $text)))
       (tg:some (tg-cmd:reply "" (tg-cmd:message-text $text) ())))
      ((JsonArray ((JsonString "menu") (JsonString $toast) (JsonString $text) (JsonArray $rows)))
       (case (tg-cmd:keyboard $rows)
         (((tg:some $keyboard) (tg:some (tg-cmd:reply (tg-cmd:toast $toast) (tg-cmd:message-text $text) $keyboard)))
          ($bad tg:none))))
      ($other tg:none))))

(= (tg-cmd:send $chat $thread $message $text $keyboard)
   (if (== $keyboard ()) (telegram:send-text 1 $chat $thread $message $text "plain")
     (telegram:send-text 1 $chat $thread $message $text "plain" $keyboard)))

(= (tg-cmd:edit $chat $message $text $keyboard)
   (if (== $keyboard ()) (telegram:edit-text 1 $chat $message $text "plain")
     (telegram:edit-text 1 $chat $message $text "plain" $keyboard)))

; A tap on a button: the query's ID, its data and the ID of the message that
; carried the button. The ID and data need no JSON escape; anything else is
; no tap.
(= (tg-cmd:tap $update)
   (case (telegram:field $update "callback_query")
     (((tg:some $query)
       (case ((telegram:field $query "id") (telegram:field $query "data") (telegram:field $query "message"))
         ((((tg:some (JsonStringV1 $id)) (tg:some (JsonStringV1 $data)) (tg:some $message))
           (case ((tg-cmd:plain (JsonStringV1 $id)) (tg-cmd:plain (JsonStringV1 $data))
                  (telegram:optional-id $message "message_id" 0))
             ((((tg:some $query-id) (tg:some $text) (tg:integer $message-id))
               (if (and (> $message-id 0)
                        (and (and (> (str:byte-length $query-id) 0) (<= (str:byte-length $query-id) 64))
                             (and (> (str:byte-length $text) 0) (<= (str:byte-length $text) 64))))
                 (tg:some (tg-cmd:tap $query-id $text $message-id))
                 tg:none))
              ($other tg:none))))
          ($other tg:none))))
      ($other tg:none))))

; The agent's task for a tap: ["callback", key, data, lane, message].
(= (tg-cmd:tap-observation $key $data $lane $message)
   (format-args "[\"callback\",\"{}\",\"{}\",\"{}\",{}]" ($key $data $lane $message)))

(= (tg-cmd:operator-only-text) "These buttons are for operators.")

(= (tg-cmd:tap-fallback-text)
   (let $agent (tg-cmd:agent-name)
     (format-args "No answer from {} yet." ($agent))))

; The bot's own username, from its getMe answer, so that /command@username
; is this bot's command. Only an answer about this bot counts: ok, and its
; ID the credential's own (ID, digits as text). The host checks the name's
; characters before declaring it; anything else is no username.
(= (tg-cmd:identity $body $id)
   (case (telegram:normalize (json:parse $body) 0)
     (((tg:ok $answer)
       (case ((telegram:field $answer "ok") (telegram:field $answer "result"))
         ((((tg:some (JsonBoolV1 True)) (tg:some $me))
           (case ((telegram:field $me "id") (telegram:field $me "username"))
             ((((tg:some $number) (tg:some (JsonStringV1 $scalars)))
               (case ((telegram:integer $number) (tg-cmd:plain (JsonStringV1 $scalars)))
                 ((((tg:integer $n) (tg:some $name))
                   (if (== (format-args "{}" ($n)) $id) (tg:some $name) tg:none))
                  ($other tg:none))))
              ($other tg:none))))
          ($other tg:none))))
      ($other tg:none))))
