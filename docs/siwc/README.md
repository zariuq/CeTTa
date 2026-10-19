# Native Sign in with ChatGPT

This optional Linux host library implements the public OpenAI open-source
Sign in with ChatGPT flow. It uses browser authorization with S256 PKCE, a
loopback callback and an issued client ID. It works in `BUILD=core` without
Python or Prolog. Authentication is a provider in the existing owned I/O
interface; only public JSON metadata reaches MeTTa.

## Build

The default build omits this library and has no JOSE dependencies. To enable
it, install libcurl development headers, cJOSE **0.8.1 or newer**, Jansson and
OpenSSL development headers, then run:

```sh
make BUILD=core ENABLE_HTTP=1 ENABLE_SIWC=1
```

`BUILD=python` enables the existing Python embedding as well; godelclaw currently
uses that build for its remaining host callbacks. Authentication itself stays
native. `ENABLE_LIB_PROLOG=0` can independently disable Prolog.

When cJOSE is unavailable from the system package manager, this is the tested
static dependency combination. Both projects are MIT licensed:
[cJOSE 0.8.1](https://github.com/cisco/cjose/tree/0.8.1),
[Jansson 2.14.1](https://github.com/akheron/jansson/tree/v2.14.1).
Build dependencies also include CMake, a C compiler and OpenSSL headers.

```sh
mkdir -p runtime
SIWC_DEPS_PREFIX="$PWD/runtime/siwc-deps"
git clone --branch v2.14.1 --depth 1 https://github.com/akheron/jansson.git runtime/siwc-jansson
cmake -S runtime/siwc-jansson -B runtime/siwc-jansson-build \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX="$SIWC_DEPS_PREFIX" \
  -DJANSSON_BUILD_SHARED_LIBS=OFF -DJANSSON_BUILD_DOCS=OFF -DJANSSON_WITHOUT_TESTS=ON
cmake --build runtime/siwc-jansson-build
cmake --install runtime/siwc-jansson-build
git clone --branch 0.8.1 --depth 1 https://github.com/cisco/cjose.git runtime/siwc-cjose
cmake -S runtime/siwc-cjose -B runtime/siwc-cjose-build \
  -DCMAKE_INSTALL_PREFIX="$SIWC_DEPS_PREFIX" -DCMAKE_PREFIX_PATH="$SIWC_DEPS_PREFIX" \
  -DCJOSE_BUILD_TESTS=OFF -DCJOSE_BUILD_SHARED=OFF -DCJOSE_BUILD_STATIC=ON
cmake --build runtime/siwc-cjose-build
cmake --install runtime/siwc-cjose-build
export PKG_CONFIG_PATH="$SIWC_DEPS_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
make BUILD=core ENABLE_HTTP=1 ENABLE_SIWC=1
```

The qualified source revisions are cJOSE
`6fdb5fe75a86acb9cd34ca2eb13d9e090a6df588` and Jansson
`ed5cae4ed0621ef409510f94270c9f8f263736d0`. Static redistribution must retain
their [license notices](licenses/) and those of the selected OpenSSL/libcurl distribution.
Dependencies are not downloaded automatically during an ordinary CeTTa build.

## Sign in and discover accounts

```sh
./cetta --lang petta --quiet examples/siwc_sign_in.metta
```

The command prints a public authorization link, keeps the owned operation alive
while consent happens in the browser, then prints a validated registration
result. Credentials never appear in that result. A remote browser needs a
loopback tunnel for the chosen port; set `CETTA_SIWC_PORT` to choose a fixed
port. `CETTA_SIWC_REGISTRATION` defaults to `new`; set a returned registration
ID to reauthorize it. `CETTA_SIWC_DEADLINE_MS` defaults to 300000, with zero
meaning no overall deadline. The listener binds before exposing the link.

Equivalent MeTTa calls:

```metta
!(import! &self siwc)
!(siwc:sign-in "new" 0 300000)         ; (io:pending id)
!(siwc:status id)                      ; public progress; does not consume
!(siwc:observe id 250)                 ; JSON result or (io:idle)
!(siwc:accounts)                       ; another owned pending handle
!(siwc:ensure "r_..." 30000)           ; fresh session or serialized refresh
!(siwc:sign-out "r_..." 30000)         ; revoke and clear, retaining identity
```

As with other I/O providers, consume each terminal event once. `io:cancel id`
cancels and releases the listener/worker. `io:wait-ready` can observe readiness
without consuming an event. Workers perform network and cryptographic work;
they never touch evaluator atoms. A native private curl handle is used for
each authentication request, without redirects or automatic POST retries.

## Credentials and inference

Records default to `$XDG_CONFIG_HOME/cetta/siwc`, or
`$HOME/.config/cetta/siwc`; `CETTA_SIWC_STORE` chooses another directory. The
store requires an owner-only directory and owner-only regular record files.
Writes use private temporary files, file fsync, atomic replacement and directory
fsync. A separate file lock and an in-process mutex serialize rotating refresh.
Each host ID is published atomically and retained across registrations.

Signature checks use cJOSE/OpenSSL, not a local RSA/JWS implementation. The
profile checks RS256, JWKS key identity, issuer, audience, authorized party,
expiry, issued-at, nonce and returning subject. The provider's
`earliest_refresh_at` value is retained without assuming an undocumented
representation. Fresh JWKS are fetched for validation; there is no cache yet.

The only inference credential passed to MeTTa is
`(http:credential "SIWC:r_...")`. Native HTTP resolves it locally for the
canonical public OpenAI `/v1/` scope, refuses credential redirects/overrides,
and applies its existing raw-token reflection check. Resolve only after an
owned `siwc:ensure`: resolution never performs blocking network renewal on the
evaluator thread. Tokens never enter guest atoms or the Python embedding.
The library does not import another application's token file.

Use the public `/v1/models` and `/v1/responses` routes. Account model discovery
uses the `models` array, retaining entries with `visibility: "list"` in order
and using `slug` as the request model. The ChatGPT-plan Responses profile is
streaming, stateless (`store: false`), with array input and its own option/tool
constraints; it is distinct from ordinary API-key Chat Completions or Responses.
The godelclaw command-loop integration supplies that profile.

An uncertain refresh leaves the previous complete record and an explicit
`refresh-outcome-unknown` state, preventing replay of a possibly rotated token.
A still-valid access token remains usable until expiry. Reauthorization can
recover the registration. `invalid_grant` clears the session but keeps its
client/verified identity. Sign-out removes local session tokens even when
revocation cannot be confirmed. Nothing silently selects a different account,
model or provider.

## Qualification

```sh
make ENABLE_HTTP=1 ENABLE_SIWC=1 test-siwc
```

The tests use generated fixture RSA keys and a loopback identity provider.
They exercise PKCE, callback validation/cancellation, signed claims, durable
registration, concurrent refresh, injected write/crash points, lost refresh,
reauthorization, revocation and credential scoping. No paid provider is called.

For the MeTTa browser-command fixture, build **only a test binary** with
`SIWC_TEST=1`, then set `SIWC_ENGINE` to that binary while running the tests.
The same flag enables a test-only loopback issuer override and store fault
injection; production builds must leave `SIWC_TEST=0`. Sanitizer builds of
`tests/siwc_driver.c` qualify native authentication independently of the
evaluator. Real-account sign-in, model visibility, inference and eventual
provider refresh still require separate live qualification.

Protocol references:
[OpenAI sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in),
[sessions](https://developers.openai.com/siwc/token-sharing-open-source/profiles-and-sessions),
[token reference](https://developers.openai.com/siwc/token-sharing-open-source/token-reference),
[model and inference rules](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference),
[preview limitations](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations).
