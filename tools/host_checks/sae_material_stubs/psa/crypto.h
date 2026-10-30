#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int32_t psa_status_t;
typedef uint32_t mbedtls_svc_key_id_t;
typedef struct {uint32_t type,bits,usage,algorithm,lifetime;} psa_key_attributes_t;
#define PSA_KEY_ATTRIBUTES_INIT {0}
#define MBEDTLS_SVC_KEY_ID_INIT 0U
#define PSA_SUCCESS 0
#define PSA_ERROR_NOT_SUPPORTED -134
#define PSA_ERROR_INSUFFICIENT_MEMORY -141
#define PSA_ERROR_HARDWARE_FAILURE -147
#define PSA_KEY_LIFETIME_VOLATILE 0U
#define PSA_ECC_FAMILY_SECP_R1 0x12U
#define PSA_KEY_TYPE_ECC_KEY_PAIR(family) (0x7100U|(family))
#define PSA_KEY_USAGE_EXPORT 1U
#define PSA_ALG_SHA_256 0x02000009U
#define PSA_ALG_ECDSA(hash) (0x06000600U|(hash))
extern unsigned attribute_resets;
static inline void psa_set_key_lifetime(psa_key_attributes_t *a,uint32_t v){a->lifetime=v;}
static inline void psa_set_key_type(psa_key_attributes_t *a,uint32_t v){a->type=v;}
static inline void psa_set_key_bits(psa_key_attributes_t *a,uint32_t v){a->bits=v;}
static inline void psa_set_key_usage_flags(psa_key_attributes_t *a,uint32_t v){a->usage=v;}
static inline void psa_set_key_algorithm(psa_key_attributes_t *a,uint32_t v){a->algorithm=v;}
static inline void psa_reset_key_attributes(psa_key_attributes_t *a){++attribute_resets;*a=(psa_key_attributes_t){0};}
psa_status_t psa_crypto_init(void);
psa_status_t psa_generate_key(const psa_key_attributes_t *,mbedtls_svc_key_id_t *);
psa_status_t psa_export_public_key(mbedtls_svc_key_id_t,uint8_t *,size_t,size_t *);
psa_status_t psa_export_key(mbedtls_svc_key_id_t,uint8_t *,size_t,size_t *);
psa_status_t psa_destroy_key(mbedtls_svc_key_id_t);
