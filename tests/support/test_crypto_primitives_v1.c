/* The byte-level primitives of lib/crypto against their published vectors:
 * HMAC-SHA256 (RFC 4231, cases 1 to 7), ChaCha20 (RFC 8439, 2.3.2 and
 * 2.4.2), and from the NIP-44 v2 vectors the padding table, every message-key
 * derivation and one payload, which must encrypt byte for byte, decrypt, and
 * refuse each tampered byte.  Base64 decoding must accept and refuse what
 * Python's b64decode(validate=True) does. */
#include "crypto_primitives.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define check(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static size_t unhex(const char *hex, uint8_t *out) {
    size_t n = strlen(hex) / 2u;
    for (size_t i = 0u; i < n; i++) {
        unsigned value;
        sscanf(hex + 2u * i, "%2x", &value);
        out[i] = (uint8_t)value;
    }
    return n;
}

static bool equal_hex(const uint8_t *bytes, size_t length, const char *hex) {
    uint8_t want[512];
    return strlen(hex) == 2u * length && unhex(hex, want) == length && !memcmp(bytes, want, length);
}

static void hmac_case(const uint8_t *key, size_t key_len, const uint8_t *data, size_t length,
                      const char *want) {
    uint8_t out[32];
    const uint8_t *parts[1] = {data};
    size_t lengths[1] = {length};
    cetta_crypto_hmac_sha256(key, key_len, parts, lengths, 1u, out);
    check(equal_hex(out, 32u, want));
    /* The same message split into parts gives the same MAC. */
    if (length > 1u) {
        const uint8_t *split[3] = {data, data + 1u, data + 1u};
        size_t split_lengths[3] = {1u, 0u, length - 1u};
        uint8_t again[32];
        cetta_crypto_hmac_sha256(key, key_len, split, split_lengths, 3u, again);
        check(!memcmp(out, again, 32u));
    }
}

static void hmac_cases(void) {
    uint8_t key[131], data[160];
    memset(key, 0x0b, 20u);
    hmac_case(key, 20u, (const uint8_t *)"Hi There", 8u,
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    hmac_case((const uint8_t *)"Jefe", 4u, (const uint8_t *)"what do ya want for nothing?", 28u,
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    memset(key, 0xaa, 20u);
    memset(data, 0xdd, 50u);
    hmac_case(key, 20u, data, 50u, "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    for (unsigned i = 0u; i < 25u; i++) key[i] = (uint8_t)(i + 1u);
    memset(data, 0xcd, 50u);
    hmac_case(key, 25u, data, 50u, "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    memset(key, 0x0c, 20u);
    hmac_case(key, 20u, (const uint8_t *)"Test With Truncation", 20u,
              "a3b6167473100ee06e0c796c2955552bfa6f7c0a6a8aef8b93f860aab0cd20c5");
    memset(key, 0xaa, 131u);
    const char *large_key = "Test Using Larger Than Block-Size Key - Hash Key First";
    hmac_case(key, 131u, (const uint8_t *)large_key, strlen(large_key),
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    const char *large_data = "This is a test using a larger than block-size key and a larger than "
        "block-size data. The key needs to be hashed before being used by the HMAC algorithm.";
    hmac_case(key, 131u, (const uint8_t *)large_data, strlen(large_data),
              "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
}

static void chacha_cases(void) {
    uint8_t key[32], nonce[12], zero[64] = {0}, block[64], text[114];
    for (unsigned i = 0u; i < 32u; i++) key[i] = (uint8_t)i;
    unhex("000000090000004a00000000", nonce);
    cetta_crypto_chacha20(key, nonce, 1u, zero, block, sizeof(block));
    check(equal_hex(block, 64u, "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e"
                                "d2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e"));
    const char *sunscreen = "Ladies and Gentlemen of the class of '99: If I could offer you only one "
        "tip for the future, sunscreen would be it.";
    check(strlen(sunscreen) == sizeof(text));
    unhex("000000000000004a00000000", nonce);
    cetta_crypto_chacha20(key, nonce, 1u, (const uint8_t *)sunscreen, text, sizeof(text));
    check(equal_hex(text, sizeof(text),
        "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b357"
        "1639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937"
        "365af90bbf74a35be6b40b8eedf2785e42874d"));
    /* Decryption is the same stream, also in place. */
    cetta_crypto_chacha20(key, nonce, 1u, text, text, sizeof(text));
    check(!memcmp(text, sunscreen, sizeof(text)));
}

static const size_t padded[][2] = {{16, 32}, {32, 32}, {33, 64}, {37, 64}, {45, 64}, {49, 64}, {64, 64}, {65, 96}, {100, 128}, {111, 128}, {200, 224}, {250, 256}, {320, 320}, {383, 384}, {384, 384}, {400, 448}, {500, 512}, {512, 512}, {515, 640}, {700, 768}, {800, 896}, {900, 1024}, {1020, 1024}, {65536, 65536}};

static const char *const message_keys[][4] = {
    {"e1e6f880560d6d149ed83dcc7e5861ee62a5ee051f7fde9975fe5d25d2a02d72", "f145f3bed47cb70dbeaac07f3a3fe683e822b3715edb7c4fe310829014ce7d76", "c4ad129bb01180c0933a160c", "027c1db445f05e2eee864a0975b0ddef5b7110583c8c192de3732571ca5838c4"},
    {"e1d6d28c46de60168b43d79dacc519698512ec35e8ccb12640fc8e9f26121101", "e35b88f8d4a8f1606c5082f7a64b100e5d85fcdb2e62aeafbec03fb9e860ad92", "22925e920cee4a50a478be90", "46a7c55d4283cb0df1d5e29540be67abfe709e3b2e14b7bf9976e6df994ded30"},
    {"cfc13bef512ac9c15951ab00030dfaf2626fdca638dedb35f2993a9eeb85d650", "020783eb35fdf5b80ef8c75377f4e937efb26bcbad0e61b4190e39939860c4bf", "d3594987af769a52904656ac", "237ec0ccb6ebd53d179fa8fd319e092acff599ef174c1fdafd499ef2b8dee745"},
    {"ea6eb84cac23c5c1607c334e8bdf66f7977a7e374052327ec28c6906cbe25967", "ff68db24b34fa62c78ac5ffeeaf19533afaedf651fb6a08384e46787f6ce94be", "50bb859aa2dde938cc49ec7a", "06ff32e1f7b29753a727d7927b25c2dd175aca47751462d37a2039023ec6b5a6"},
    {"8c2e1dd3792802f1f9f7842e0323e5d52ad7472daf360f26e15f97290173605d", "2f9daeda8683fdeede81adac247c63cc7671fa817a1fd47352e95d9487989d8b", "400224ba67fc2f1b76736916", "465c05302aeeb514e41c13ed6405297e261048cfb75a6f851ffa5b445b746e4b"},
    {"05c28bf3d834fa4af8143bf5201a856fa5fac1a3aee58f4c93a764fc2f722367", "1e3d45777025a035be566d80fd580def73ed6f7c043faec2c8c1c690ad31c110", "021905b1ea3afc17cb9bf96f", "74a6e481a89dcd130aaeb21060d7ec97ad30f0007d2cae7b1b11256cc70dfb81"},
    {"5e043fb153227866e75a06d60185851bc90273bfb93342f6632a728e18a07a17", "1ea72c9293841e7737c71567d8120145a58991aaa1c436ef77bf7adb83f882f1", "72f69a5a5f795465cee59da8", "e9daa1a1e9a266ecaa14e970a84bce3fbbf329079bbccda626582b4e66a0d4c9"},
    {"7be7338eaf06a87e274244847fe7a97f5c6a91f44adc18fcc3e411ad6f786dbf", "881e7968a1f0c2c80742ee03cd49ea587e13f22699730f1075ade01931582bf6", "6e69be92d61c04a276021565", "901afe79e74b19967c8829af23617d7d0ffbf1b57190c096855c6a03523a971b"},
    {"94571c8d590905bad7becd892832b472f2aa5212894b6ce96e5ba719c178d976", "f80873dd48466cb12d46364a97b8705c01b9b4230cb3ec3415a6b9551dc42eef", "3dda53569cfcb7fac1805c35", "e9fc264345e2839a181affebc27d2f528756e66a5f87b04bf6c5f1997047051e"},
    {"13a6ee974b1fd759135a2c2010e3cdda47081c78e771125e4f0c382f0284a8cb", "bc5fb403b0bed0d84cf1db872b6522072aece00363178c98ad52178d805fca85", "65064239186e50304cc0f156", "e872d320dde4ed3487958a8e43b48aabd3ced92bc24bb8ff1ccb57b590d9701a"},
    {"082fecdb85f358367b049b08be0e82627ae1d8edb0f27327ccb593aa2613b814", "1fbdb1cf6f6ea816349baf697932b36107803de98fcd805ebe9849b8ad0e6a45", "2e605e1d825a3eaeb613db9c", "fae910f591cf3c7eb538c598583abad33bc0a03085a96ca4ea3a08baf17c0eec"},
    {"4c19020c74932c30ec6b2d8cd0d5bb80bd0fc87da3d8b4859d2fb003810afd03", "1ab9905a0189e01cda82f843d226a82a03c4f5b6dbea9b22eb9bc953ba1370d4", "cbb2530ea653766e5a37a83a", "267f68acac01ac7b34b675e36c2cef5e7b7a6b697214add62a491bedd6efc178"},
    {"67723a3381497b149ce24814eddd10c4c41a1e37e75af161930e6b9601afd0ff", "9ecbd25e7e2e6c97b8c27d376dcc8c5679da96578557e4e21dba3a7ef4e4ac07", "ef649fcf335583e8d45e3c2e", "04dbbd812fa8226fdb45924c521a62e3d40a9e2b5806c1501efdeba75b006bf1"},
    {"42063fe80b093e8619b1610972b4c3ab9e76c14fd908e642cd4997cafb30f36c", "211c66531bbcc0efcdd0130f9f1ebc12a769105eb39608994bcb188fa6a73a4a", "67803605a7e5010d0f63f8c8", "e840e4e8921b57647369d121c5a19310648105dbdd008200ebf0d3b668704ff8"},
    {"b5ac382a4be7ac03b554fe5f3043577b47ea2cd7cfc7e9ca010b1ffbb5cf1a58", "b3b5f14f10074244ee42a3837a54309f33981c7232a8b16921e815e1f7d1bb77", "4e62a0073087ed808be62469", "c8efa10230b5ea11633816c1230ca05fa602ace80a7598916d83bae3d3d2ccd7"},
    {"e9d1eba47dd7e6c1532dc782ff63125db83042bb32841db7eeafd528f3ea7af9", "54241f68dc2e50e1db79e892c7c7a471856beeb8d51b7f4d16f16ab0645d2f1a", "a963ed7dc29b7b1046820a1d", "aba215c8634530dc21c70ddb3b3ee4291e0fa5fa79be0f85863747bde281c8b2"},
    {"a94ecf8efeee9d7068de730fad8daf96694acb70901d762de39fa8a5039c3c49", "c0565e9e201d2381a2368d7ffe60f555223874610d3d91fbbdf3076f7b1374dd", "329bb3024461e84b2e1c489b", "ac42445491f092481ce4fa33b1f2274700032db64e3a15014fbe8c28550f2fec"},
    {"533605ea214e70c25e9a22f792f4b78b9f83a18ab2103687c8a0075919eaaa53", "ab35a5e1e54d693ff023db8500d8d4e79ad8878c744e0eaec691e96e141d2325", "653d759042b85194d4d8c0a7", "b43628e37ba3c31ce80576f0a1f26d3a7c9361d29bb227433b66f49d44f167ba"},
    {"7f38df30ceea1577cb60b355b4f5567ff4130c49e84fed34d779b764a9cc184c", "a37d7f211b84a551a127ff40908974eb78415395d4f6f40324428e850e8c42a3", "b822e2c959df32b3cb772a7c", "1ba31764f01f69b5c89ded2d7c95828e8052c55f5d36f1cd535510d61ba77420"},
    {"11b37f9dbc4d0185d1c26d5f4ed98637d7c9701fffa65a65839fa4126573a4e5", "964f38d3a31158a5bfd28481247b18dd6e44d69f30ba2a40f6120c6d21d8a6ba", "5f72c5b87c590bcd0f93b305", "2fc4553e7cedc47f29690439890f9f19c1077ef3e9eaeef473d0711e04448918"},
    {"8be790aa483d4cdd843189f71f135b3ec7e31f381312c8fe9f177aab2a48eafa", "95c8c74d633721a131316309cf6daf0804d59eaa90ea998fc35bac3d2fbb7a94", "409a7654c0e4bf8c2c6489be", "21bb0b06eb2b460f8ab075f497efa9a01c9cf9146f1e3986c3bf9da5689b6dc4"},
    {"19fd2a718ea084827d6bd73f509229ddf856732108b59fc01819f611419fd140", "cc6714b9f5616c66143424e1413d520dae03b1a4bd202b82b0a89b0727f5cdc8", "1b7fd2534f015a8f795d8f32", "2bef39c4ce5c3c59b817e86351373d1554c98bc131c7e461ed19d96cfd6399a0"},
    {"3c2acd893952b2f6d07d8aea76f545ca45961a93fe5757f6a5a80811d5e0255d", "c8de6c878cb469278d0af894bc181deb6194053f73da5014c2b5d2c8db6f2056", "6ffe4f1971b904a1b1a81b99", "df1cd69dd3646fca15594284744d4211d70e7d8472e545d276421fbb79559fd4"},
    {"7dbea4cead9ac91d4137f1c0a6eebb6ba0d1fb2cc46d829fbc75f8d86aca6301", "c8e030f6aa680c3d0b597da9c92bb77c21c4285dd620c5889f9beba7446446b0", "a9b5a67d081d3b42e737d16f", "355a85f551bc3cce9a14461aa60994742c9bbb1c81a59ca102dc64e61726ab8e"},
    {"45422e676cdae5f1071d3647d7a5f1f5adafb832668a578228aa1155a491f2f3", "758437245f03a88e2c6a32807edfabff51a91c81ca2f389b0b46f2c97119ea90", "263830a065af33d9c6c5aa1f", "7c581cf3489e2de203a95106bfc0de3d4032e9d5b92b2b61fb444acd99037e17"},
    {"babc0c03fad24107ad60678751f5db2678041ff0d28671ede8d65bdf7aa407e9", "bd68a28bd48d9ffa3602db72c75662ac2848a0047a313d2ae2d6bc1ac153d7e9", "d0f9d2a1ace6c758f594ffdd", "eb435e3a642adfc9d59813051606fc21f81641afd58ea6641e2f5a9f123bb50a"},
    {"7a1b8aac37d0d20b160291fad124ab697cfca53f82e326d78fef89b4b0ea8f83", "9e97875b651a1d30d17d086d1e846778b7faad6fcbc12e08b3365d700f62e4fe", "ccdaad5b3b7645be430992eb", "6f2f55cf35174d75752f63c06cc7cbc8441759b142999ed2d5a6d09d263e1fc4"},
    {"8370e4e32d7e680a83862cab0da6136ef607014d043e64cdf5ecc0c4e20b3d9a", "1472bed5d19db9c546106de946e0649cd83cc9d4a66b087a65906e348dcf92e2", "ed02dece5fc3a186f123420b", "7b3f7739f49d30c6205a46b174f984bb6a9fc38e5ccfacef2dac04fcbd3b184e"},
    {"9f1c5e8a29cd5677513c2e3a816551d6833ee54991eb3f00d5b68096fc8f0183", "5e1a7544e4d4dafe55941fcbdf326f19b0ca37fc49c4d47e9eec7fb68cde4975", "7d9acb0fdc174e3c220f40de", "e265ab116fbbb86b2aefc089a0986a0f5b77eda50c7410404ad3b4f3f385c7a7"},
    {"c385aa1c37c2bfd5cc35fcdbdf601034d39195e1cabff664ceb2b787c15d0225", "06bf4e60677a13e54c4a38ab824d2ef79da22b690da2b82d0aa3e39a14ca7bdd", "26b450612ca5e905b937e147", "22208152be2b1f5f75e6bfcc1f87763d48bb7a74da1be3d102096f257207f8b3"},
    {"3ff73528f88a50f9d35c0ddba4560bacee5b0462d0f4cb6e91caf41847040ce4", "850c8a17a23aa761d279d9901015b2bbdfdff00adbf6bc5cf22bd44d24ecabc9", "4a296a1fb0048e5020d3b129", "b1bf49a533c4da9b1d629b7ff30882e12d37d49c19abd7b01b7807d75ee13806"},
    {"2dcf39b9d4c52f1cb9db2d516c43a7c6c3b8c401f6a4ac8f131a9e1059957036", "17f8057e6156ba7cc5310d01eda8c40f9aa388f9fd1712deb9511f13ecc37d27", "a8188daff807a1182200b39d", "47b89da97f68d389867b5d8a2d7ba55715a30e3d88a3cc11f3646bc2af5580ef"}
};

static void nip44_cases(void) {
    uint8_t conversation[32], nonce[32], keys[76];
    for (size_t i = 0u; i < sizeof(padded) / sizeof(*padded); i++)
        check(cetta_crypto_nip44_padded_length(padded[i][0]) == padded[i][1]);
    unhex("a1a3d60f3470a8612633924e91febf96dc5366ce130f658b1f0fc652c20b3b54", conversation);
    for (size_t i = 0u; i < sizeof(message_keys) / sizeof(*message_keys); i++) {
        unhex(message_keys[i][0], nonce);
        cetta_crypto_nip44_message_keys(conversation, nonce, keys);
        check(equal_hex(keys, 32u, message_keys[i][1]));
        check(equal_hex(keys + 32u, 12u, message_keys[i][2]));
        check(equal_hex(keys + 44u, 32u, message_keys[i][3]));
    }

    const char *want = "AgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABee0G5VSK0/9YypIObAtDKfYEAjD35uVkHyB0F4DwrcNaCXlCWZKaArsGrY6M9wnuTMxWfp1RTN9Xga8no+kF5Vsb";
    char *payload;
    size_t payload_length, plain_length;
    uint8_t *plain;
    unhex("c41c775356fd92eadc63ff5a0dc1da211b268cbea22316767095b2871ea1412d", conversation);
    unhex("0000000000000000000000000000000000000000000000000000000000000001", nonce);
    check(cetta_crypto_nip44_encrypt(conversation, nonce, (const uint8_t *)"a", 1u, &payload,
                                     &payload_length) == CETTA_NIP44_OK);
    check(payload_length == strlen(want) && !strcmp(payload, want));
    check(cetta_crypto_nip44_decrypt(conversation, (const uint8_t *)payload, payload_length, &plain,
                                     &plain_length) == CETTA_NIP44_OK);
    check(plain_length == 1u && plain[0] == 'a');
    free(plain);

    /* Every byte of the data matters: a changed version byte is an unknown
     * version, and any other change fails the MAC. */
    uint8_t data[200];
    size_t data_length;
    check(cetta_crypto_base64_decode((const uint8_t *)payload, payload_length, data, &data_length));
    for (size_t at = 0u; at < data_length; at++) {
        uint8_t tampered[200];
        char text[300];
        memcpy(tampered, data, data_length);
        tampered[at] ^= 0x01u;
        size_t text_length = cetta_crypto_base64_encode(tampered, data_length, text);
        CettaNip44Status status = cetta_crypto_nip44_decrypt(conversation, (const uint8_t *)text,
                                                             text_length, &plain, &plain_length);
        check(status == (at == 0u ? CETTA_NIP44_UNKNOWN_VERSION : CETTA_NIP44_INVALID_MAC));
    }
    free(payload);

    check(cetta_crypto_nip44_encrypt(conversation, nonce, (const uint8_t *)"", 0u, &payload,
                                     &payload_length) == CETTA_NIP44_INVALID_PLAINTEXT_LENGTH);
    check(cetta_crypto_nip44_decrypt(conversation, (const uint8_t *)"", 0u, &plain,
                                     &plain_length) == CETTA_NIP44_UNKNOWN_VERSION);
    check(cetta_crypto_nip44_decrypt(conversation, (const uint8_t *)"#abc", 4u, &plain,
                                     &plain_length) == CETTA_NIP44_UNKNOWN_VERSION);
    check(cetta_crypto_nip44_decrypt(conversation, (const uint8_t *)"Ag==", 4u, &plain,
                                     &plain_length) == CETTA_NIP44_INVALID_PAYLOAD_SIZE);
}

/* Python's b64decode(validate=True), in Python 3.12 and 3.13. */
static void base64_cases(void) {
    static const struct { const char *text; const char *bytes; } accepted[] = {
        {"QUJD", "ABC"}, {"QUI=", "AB"}, {"QU==", "A"}, {"QUJ=", "AB"}, {"QR==", "A"}, {"", ""},
    };
    static const char *const refused[] = {
        "QUI", "Q===", "QUJD=", "QUJ D", "QUJ\n", "====", "QUJDRA==QUJD", "QUI=QUJD", "QU=a", "Q\xd1\x84==",
    };
    uint8_t out[32];
    size_t length;
    for (size_t i = 0u; i < sizeof(accepted) / sizeof(*accepted); i++) {
        check(cetta_crypto_base64_decode((const uint8_t *)accepted[i].text, strlen(accepted[i].text), out, &length));
        check(length == strlen(accepted[i].bytes) && !memcmp(out, accepted[i].bytes, length));
    }
    for (size_t i = 0u; i < sizeof(refused) / sizeof(*refused); i++)
        check(!cetta_crypto_base64_decode((const uint8_t *)refused[i], strlen(refused[i]), out, &length));
    char text[64];
    for (size_t n = 0u; n <= 7u; n++) {
        const uint8_t bytes[7] = {0xffu, 0x00u, 0x10u, 0x83u, 0xbfu, 0xfeu, 0x7fu};
        size_t text_length = cetta_crypto_base64_encode(bytes, n, text);
        check(text_length == (n + 2u) / 3u * 4u && strlen(text) == text_length);
        check(cetta_crypto_base64_decode((const uint8_t *)text, text_length, out, &length));
        check(length == n && !memcmp(out, bytes, n));
    }
}

static void entropy_cases(void) {
    uint8_t first[600], second[600];
    check(cetta_crypto_entropy(first, sizeof(first)));
    check(cetta_crypto_entropy(second, sizeof(second)));
    check(memcmp(first, second, sizeof(first)) != 0);
    check(cetta_crypto_entropy(first, 0u));
}

int main(void) {
    hmac_cases();
    chacha_cases();
    nip44_cases();
    base64_cases();
    entropy_cases();
    printf("crypto primitives: %u checks\n", checks);
    return 0;
}
