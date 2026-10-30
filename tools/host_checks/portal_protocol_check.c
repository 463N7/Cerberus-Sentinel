#ifdef NDEBUG
#undef NDEBUG
#endif
#include "portal_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t query_a[] = {
    0x12,0x34,0x01,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
    0x04,'t','e','s','t',0x05,'l','o','c','a','l',0x00,
    0x00,0x01,0x00,0x01,
};

static void dns_checks(void)
{
    uint8_t address[4] = {172,0,0,1};
    uint8_t guarded[600];
    memset(guarded, 0xA5, sizeof(guarded));
    uint8_t *reply = guarded + 1;
    size_t size = cerberus_portal_dns_reply(query_a, sizeof(query_a), address,
        reply, sizeof(guarded) - 2U);
    assert(size == sizeof(query_a) + 16U);
    assert(guarded[0] == 0xA5 && guarded[size + 1U] == 0xA5);
    assert(reply[0] == 0x12 && reply[1] == 0x34);
    assert(reply[2] == 0x81 && reply[3] == 0x80);
    assert(reply[6] == 0x00 && reply[7] == 0x01);
    assert(reply[sizeof(query_a)] == 0xC0 && reply[sizeof(query_a)+1U] == 0x0C);
    assert(!memcmp(reply + size - 4U, address, 4U));
    assert(cerberus_portal_dns_reply(query_a, sizeof(query_a), address, reply, size - 1U) == 0U);
    uint8_t modified[sizeof(query_a)];
    memcpy(modified, query_a, sizeof(modified));
    modified[sizeof(modified)-3U] = 28; /* AAAA gets a valid empty answer. */
    size = cerberus_portal_dns_reply(modified, sizeof(modified), address, reply, sizeof(guarded)-2U);
    assert(size == sizeof(query_a) && reply[6] == 0 && reply[7] == 0);
    for (size_t length = 0U; length < sizeof(query_a); ++length)
        assert(cerberus_portal_dns_reply(query_a, length, address, reply,
            sizeof(guarded)-2U) == 0U);
    memcpy(modified, query_a, sizeof(modified));
    modified[4] = 2; /* multi-question */
    assert(!cerberus_portal_dns_reply(modified,sizeof(modified),address,reply,sizeof(guarded)-2U));
    modified[4] = 0;
    modified[12] = 0xC0; /* compressed names in the question are rejected. */
    assert(!cerberus_portal_dns_reply(modified,sizeof(modified),address,reply,sizeof(guarded)-2U));
    modified[12] = 4;
    modified[2] = 0x81; /* already a response */
    assert(!cerberus_portal_dns_reply(modified,sizeof(modified),address,reply,sizeof(guarded)-2U));
    assert(!cerberus_portal_dns_reply(query_a,sizeof(query_a),NULL,reply,sizeof(guarded)-2U));
}

static void form_checks(void)
{
    cerberus_portal_form_field_t fields[CERBERUS_PORTAL_FORM_FIELDS];
    uint8_t count = 0U;
    const char body[] = "name=Yasin+Home&code=A%26B%3D7";
    assert(cerberus_portal_form_decode(body, sizeof(body)-1U, fields, &count));
    assert(count == 2U && strcmp(fields[0].name,"name") == 0);
    assert(strcmp(fields[0].value,"Yasin Home") == 0);
    assert(strcmp(fields[1].name,"code") == 0 && strcmp(fields[1].value,"A&B=7") == 0);
    const char *invalid[] = {"", "x", "=x", "x=", "x=%", "x=%GG", "x=a&", "x=a&y=%00",
        "x=a%0A", "x=a&y=b&z=c&a=d&b=e", "x=a&bad.name=x"};
    for (size_t i = 0U; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
        assert(!cerberus_portal_form_decode(invalid[i], strlen(invalid[i]), fields, &count));
    char too_long[100];
    memset(too_long,'A',sizeof(too_long));
    too_long[0]='x';too_long[1]='=';
    assert(!cerberus_portal_form_decode(too_long,sizeof(too_long),fields,&count));
    assert(!cerberus_portal_form_decode(body,513U,fields,&count));
}

static void html_checks(void)
{
    const uint8_t name[] = {'A','&','<','>','"','\'',0,'Z'};
    char text[64];
    assert(cerberus_portal_html_escape(name,sizeof(name),text,sizeof(text)) > 0U);
    assert(!strcmp(text,"A&amp;&lt;&gt;&quot;&#39;?Z"));
    assert(!cerberus_portal_html_escape(name,sizeof(name),text,8U));
}

int main(void)
{
    dns_checks();
    form_checks();
    html_checks();
    puts("S3 portal protocol passed: bounded DNS A/AAAA, malformed question rejection, form decoding and HTML escaping.");
}
