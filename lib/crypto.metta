; Keys, signatures and encryption for Nostr agent mail: BIP-340 Schnorr
; signatures over secp256k1 (NIP-01) and NIP-44 v2 encryption.  Every operand
; and result is text: keys, digests and signatures in lowercase hex, payloads
; in base64, plaintexts as UTF-8.  A refusal is (crypto:failed KIND DETAIL).

(: __cetta_lib_crypto (-> Atom Atom Atom))
(: crypto:xonly-public-key (-> %Undefined% Atom))
(: crypto:schnorr-sign (-> %Undefined% %Undefined% %Undefined% Atom))
(: crypto:schnorr-verify (-> %Undefined% %Undefined% %Undefined% Atom))
(: crypto:nip44-conversation-key (-> %Undefined% %Undefined% Atom))
(: crypto:nip44-encrypt (-> %Undefined% %Undefined% %Undefined% Atom))
(: crypto:nip44-decrypt (-> %Undefined% %Undefined% Atom))
(: crypto:random-hex (-> %Undefined% Atom))

; The x-only public key of a 32-byte secret key.
(= (crypto:xonly-public-key $secret)
   (function (chain (eval (__cetta_lib_crypto xonly-public-key ($secret))) $value (return $value))))

; The BIP-340 signature of a message, both in hex, with 32 bytes of auxiliary
; randomness; the same inputs give the same signature.
(= (crypto:schnorr-sign $secret $message $aux)
   (function (chain (eval (__cetta_lib_crypto schnorr-sign ($secret $message $aux))) $value (return $value))))

; True when the signature is a valid BIP-340 signature of the message under
; the x-only public key, and False for any other text.
(= (crypto:schnorr-verify $public $message $signature)
   (function (chain (eval (__cetta_lib_crypto schnorr-verify ($public $message $signature))) $value (return $value))))

; The NIP-44 v2 conversation key of a secret key and a peer's x-only public
; key: HKDF-extract, salted "nip44-v2", of their shared x coordinate.  Both
; sides of a conversation compute the same key.
(= (crypto:nip44-conversation-key $secret $public)
   (function (chain (eval (__cetta_lib_crypto nip44-conversation-key ($secret $public))) $value (return $value))))

; The NIP-44 v2 payload of a plaintext of 1 to 65535 bytes under a
; conversation key and a 32-byte nonce; a fresh nonce for every message.
(= (crypto:nip44-encrypt $key $plaintext $nonce)
   (function (chain (eval (__cetta_lib_crypto nip44-encrypt ($key $plaintext $nonce))) $value (return $value))))

; The plaintext of a NIP-44 v2 payload, or a refusal naming why it does not
; open: crypto:unknown-version, crypto:invalid-payload, crypto:invalid-mac,
; crypto:invalid-padding or crypto:invalid-utf8.
(= (crypto:nip44-decrypt $key $payload)
   (function (chain (eval (__cetta_lib_crypto nip44-decrypt ($key $payload))) $value (return $value))))

; COUNT bytes from the operating system's random source, in hex.
(= (crypto:random-hex $count)
   (function (chain (eval (__cetta_lib_crypto random-hex ($count))) $value (return $value))))
