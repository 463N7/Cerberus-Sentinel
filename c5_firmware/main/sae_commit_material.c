#include "sae_commit_material.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"
#include <stdbool.h>
#include <string.h>

/* Public PSA Crypto API in IDF 6.0.1 supplies one ephemeral P-256 key pair.
 * SAE group 19 encodes the scalar in big endian and the element as X||Y.
 * SEC1's 0x04 prefix is stripped only after exact length/format checks. */
static esp_err_t psa_error(psa_status_t status)
{
    if(status==PSA_ERROR_NOT_SUPPORTED)return ESP_ERR_NOT_SUPPORTED;
    if(status==PSA_ERROR_INSUFFICIENT_MEMORY)return ESP_ERR_NO_MEM;
    return ESP_FAIL;
}
static bool scalar_more_than_one(const uint8_t scalar[32])
{
    for(unsigned i=0;i<31;++i)if(scalar[i])return true;
    return scalar[31]>1U;
}
esp_err_t cerberus_c5_sae_material_generate(uint8_t scalar[32],
    uint8_t element_xy[64])
{
    if(!scalar||!element_xy){
        if(scalar)mbedtls_platform_zeroize(scalar,32);
        if(element_xy)mbedtls_platform_zeroize(element_xy,64);
        return ESP_ERR_INVALID_ARG;
    }
    memset(scalar,0,32);memset(element_xy,0,64);
    uint8_t private_bytes[32]={0},public_bytes[65]={0};
    size_t private_length=0,public_length=0;
    psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key=MBEDTLS_SVC_KEY_ID_INIT;
    bool generated=false;
    esp_err_t error=ESP_FAIL;
    psa_status_t status=psa_crypto_init();
    if(status!=PSA_SUCCESS){error=psa_error(status);goto done;}
    psa_set_key_lifetime(&attributes,PSA_KEY_LIFETIME_VOLATILE);
    psa_set_key_type(&attributes,
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes,256);
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status=psa_generate_key(&attributes,&key);
    if(status!=PSA_SUCCESS){error=psa_error(status);goto done;}
    generated=true;
    status=psa_export_public_key(key,public_bytes,sizeof(public_bytes),
        &public_length);
    if(status!=PSA_SUCCESS){error=psa_error(status);goto done;}
    if(public_length!=sizeof(public_bytes)||public_bytes[0]!=4U){
        error=ESP_ERR_INVALID_RESPONSE;goto done;
    }
    status=psa_export_key(key,private_bytes,sizeof(private_bytes),
        &private_length);
    if(status!=PSA_SUCCESS){error=psa_error(status);goto done;}
    if(private_length!=sizeof(private_bytes)||!scalar_more_than_one(private_bytes)){
        error=ESP_ERR_INVALID_RESPONSE;goto done;
    }
    memcpy(scalar,private_bytes,32);
    memcpy(element_xy,public_bytes+1,64);
    error=ESP_OK;
done:
    if(generated&&psa_destroy_key(key)!=PSA_SUCCESS)error=ESP_FAIL;
    psa_reset_key_attributes(&attributes);
    mbedtls_platform_zeroize(private_bytes,sizeof(private_bytes));
    mbedtls_platform_zeroize(public_bytes,sizeof(public_bytes));
    if(error!=ESP_OK){
        mbedtls_platform_zeroize(scalar,32);
        mbedtls_platform_zeroize(element_xy,64);
    }
    return error;
}
