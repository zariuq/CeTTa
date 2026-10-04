!(import! &self durable:rho)

; Pure descriptions. The host supplies a chat/method grant and validates again
; at dispatch. Routing, formatting and chunk boundaries are chosen before the
; containing transition is accepted; each accepted action keeps its identity.
(= (durable:telegram:send-text $grant $chat $thread $reply-to $text $format $reply)
   (durable:rho:request $grant (telegram:send-text 1 $chat $thread $reply-to $text $format) $reply))
(= (durable:telegram:edit-text $grant $chat $message $text $format $reply)
   (durable:rho:request $grant (telegram:edit-text 1 $chat $message $text $format) $reply))
(= (durable:telegram:delete-message $grant $chat $message $reply)
   (durable:rho:request $grant (telegram:delete-message 1 $chat $message) $reply))
