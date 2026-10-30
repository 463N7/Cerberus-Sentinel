#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "psa/crypto.h"
#include "sae_commit_material.h"

enum { GOOD, INIT_FAIL, GENERATE_FAIL, PUBLIC_FAIL, BAD_PUBLIC_LENGTH,
    BAD_PUBLIC_PREFIX, PRIVATE_FAIL, BAD_PRIVATE_LENGTH, BAD_PRIVATE_SCALAR,
    DESTROY_FAIL };
static int fault,generated,destroyed,zeroized;
unsigned attribute_resets;
static const uint8_t point_2g[65]={
    4,
    0x7c,0xf2,0x7b,0x18,0x8d,0x03,0x4f,0x7e,0x8a,0x52,0x38,0x03,0x04,0xb5,0x1a,0xc3,
    0xc0,0x89,0x69,0xe2,0x77,0xf2,0x1b,0x35,0xa6,0x0b,0x48,0xfc,0x47,0x66,0x99,0x78,
    0x07,0x77,0x55,0x10,0xdb,0x8e,0xd0,0x40,0x29,0x3d,0x9a,0xc6,0x9f,0x74,0x30,0xdb,
    0xba,0x7d,0xad,0xe6,0x3c,0xe9,0x82,0x29,0x9e,0x04,0xb7,0x9d,0x22,0x78,0x73,0xd1,
};
void mbedtls_platform_zeroize(void *data,size_t size)
{
    volatile uint8_t *p=data;
    while(size--)*p++=0;
    ++zeroized;
}
psa_status_t psa_crypto_init(void)
{return fault==INIT_FAIL?PSA_ERROR_NOT_SUPPORTED:PSA_SUCCESS;}
psa_status_t psa_generate_key(const psa_key_attributes_t *a,mbedtls_svc_key_id_t *key)
{
    assert(a->type==PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    assert(a->bits==256&&a->usage==PSA_KEY_USAGE_EXPORT&&
        a->algorithm==PSA_ALG_ECDSA(PSA_ALG_SHA_256)&&
        a->lifetime==PSA_KEY_LIFETIME_VOLATILE);
    if(fault==GENERATE_FAIL)return PSA_ERROR_INSUFFICIENT_MEMORY;
    ++generated;*key=123;return PSA_SUCCESS;
}
psa_status_t psa_export_public_key(mbedtls_svc_key_id_t key,uint8_t *out,size_t cap,size_t *length)
{
    assert(key==123&&cap==65);
    if(fault==PUBLIC_FAIL)return PSA_ERROR_HARDWARE_FAILURE;
    memcpy(out,point_2g,65);*length=fault==BAD_PUBLIC_LENGTH?64:65;
    if(fault==BAD_PUBLIC_PREFIX)out[0]=2;
    return PSA_SUCCESS;
}
psa_status_t psa_export_key(mbedtls_svc_key_id_t key,uint8_t *out,size_t cap,size_t *length)
{
    assert(key==123&&cap==32);
    if(fault==PRIVATE_FAIL)return PSA_ERROR_HARDWARE_FAILURE;
    memset(out,0,32);out[31]=fault==BAD_PRIVATE_SCALAR?1:2;
    *length=fault==BAD_PRIVATE_LENGTH?31:32;
    return PSA_SUCCESS;
}
psa_status_t psa_destroy_key(mbedtls_svc_key_id_t key)
{assert(key==123);++destroyed;return fault==DESTROY_FAIL?PSA_ERROR_HARDWARE_FAILURE:PSA_SUCCESS;}
static bool all_zero(const uint8_t *data,size_t length)
{for(size_t i=0;i<length;++i)if(data[i])return false;return true;}
static void run_case(int which,esp_err_t expected)
{
    fault=which;generated=destroyed=zeroized=0;attribute_resets=0;
    uint8_t scalar[32],element[64];memset(scalar,0xa5,32);memset(element,0xa5,64);
    assert(cerberus_c5_sae_material_generate(scalar,element)==expected);
    assert(attribute_resets==1&&zeroized>=2);
    assert(generated==(which==INIT_FAIL||which==GENERATE_FAIL?0:1));
    assert(destroyed==generated);
    if(expected==ESP_OK){
        assert(all_zero(scalar,31)&&scalar[31]==2);
        assert(!memcmp(element,point_2g+1,64));
    }else assert(all_zero(scalar,32)&&all_zero(element,64));
}
int main(void)
{
    assert(cerberus_c5_sae_material_generate(NULL,NULL)==ESP_ERR_INVALID_ARG);
    uint8_t sole_scalar[32],sole_element[64];
    memset(sole_scalar,0xa5,sizeof(sole_scalar));
    memset(sole_element,0xa5,sizeof(sole_element));
    assert(cerberus_c5_sae_material_generate(sole_scalar,NULL)==ESP_ERR_INVALID_ARG&&
        all_zero(sole_scalar,sizeof(sole_scalar)));
    assert(cerberus_c5_sae_material_generate(NULL,sole_element)==ESP_ERR_INVALID_ARG&&
        all_zero(sole_element,sizeof(sole_element)));
    run_case(GOOD,ESP_OK);
    run_case(INIT_FAIL,ESP_ERR_NOT_SUPPORTED);
    run_case(GENERATE_FAIL,ESP_ERR_NO_MEM);
    run_case(PUBLIC_FAIL,ESP_FAIL);
    run_case(BAD_PUBLIC_LENGTH,ESP_ERR_INVALID_RESPONSE);
    run_case(BAD_PUBLIC_PREFIX,ESP_ERR_INVALID_RESPONSE);
    run_case(PRIVATE_FAIL,ESP_FAIL);
    run_case(BAD_PRIVATE_LENGTH,ESP_ERR_INVALID_RESPONSE);
    run_case(BAD_PRIVATE_SCALAR,ESP_ERR_INVALID_RESPONSE);
    run_case(DESTROY_FAIL,ESP_FAIL);
    puts("SAE material passed: PSA P-256 policy, 2G serialization, all failure cleanup paths and secret zeroization.");
}
