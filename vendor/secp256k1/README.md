# libsecp256k1

libsecp256k1 0.8.0, unmodified files from the release tag. MIT license (COPYING).

Source: https://github.com/bitcoin-core/secp256k1, tag v0.8.0, commit 6e2c8bc4ecdc6e71dbe7a368f360d8d453ce435d

Kept: COPYING; the public headers secp256k1.h, secp256k1_ecdh.h,
secp256k1_extrakeys.h, secp256k1_preallocated.h and secp256k1_schnorrsig.h;
the headers under src/, secp256k1.c, the two precomputed tables, and the
main_impl.h of the ecdh, extrakeys and schnorrsig modules. The build enables
those three modules and nothing else; lib/crypto uses them for BIP-340
signatures and the NIP-44 shared point.

Updates replace these files from one release tag, after reviewing its
changelog and running `make test-crypto-v1`. Do not patch the files.
