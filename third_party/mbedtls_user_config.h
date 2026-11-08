/* Appended to Mbed TLS's default config. The app opens TLS connections from several threads at once (Discover sections, artwork,
 * downloads, streaming). With TLS 1.3 compiled in, every handshake touches the global PSA crypto state, which is only safe with
 * threading support: without it concurrent handshakes corrupt it ("TLS handshake: ERROR - This is a bug in the library"). */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
