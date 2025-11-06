/**
 * boringssl_wrapper.cc - C++ wrapper for BoringSSL C++ functions
 */

/* Forward declare BoringSSL types */
struct ssl_ctx_st;
typedef struct ssl_ctx_st SSL_CTX;

/* Forward declare BoringSSL C++ function */
namespace bssl {
    extern void SSL_CTX_set_aes_hw_override_for_testing(SSL_CTX *ctx, bool override_value);
}

extern "C" {

/* C wrapper for bssl::SSL_CTX_set_aes_hw_override_for_testing */
void httpmorph_set_aes_hw_override(SSL_CTX *ctx, int override_value) {
    bssl::SSL_CTX_set_aes_hw_override_for_testing(ctx, override_value != 0);
}

}
