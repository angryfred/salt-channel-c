#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "../src/salt_util.h"
#include "salt_v2.h"

#include "test_data.c"

void randombytes(unsigned char *p_bytes, unsigned long long length)
{
    memcpy(p_bytes, salt_test_data.host_ek_sec, length);
}

salt_ret_t my_write(salt_io_channel_t *p_wchannel)
{

    static uint8_t n = 0;

    uint32_t expected_size;
    uint8_t *expected_data;
    
    switch (n) {
        case 0:
            expected_size = sizeof(salt_test_data.m2);
            expected_data = salt_test_data.m2;
            break;
        case 1:
            expected_size = sizeof(salt_test_data.m3);
            expected_data = salt_test_data.m3;
            break;
        case 2:
            expected_size = sizeof(salt_test_data.msg2);
            expected_data = salt_test_data.msg2;
            break;
    }

    assert(p_wchannel->size_expected == expected_size);
    assert(memcmp(expected_data, p_wchannel->p_data, p_wchannel->size_expected) == 0);
    n++;

    p_wchannel->size = p_wchannel->size_expected;
    
    return SALT_SUCCESS;
}

salt_ret_t my_read(salt_io_channel_t *p_rchannel)
{
    uint32_t expected_size;
    uint8_t *host_input;
    uint8_t offset = 0;

    static uint8_t n = 0;

    switch (n) {
        case 0:
            expected_size = 4;
            host_input = salt_test_data.m1;
            break;
        case 1:
            offset = 4;
            expected_size = sizeof(salt_test_data.m1) - offset;
            host_input = &salt_test_data.m1[offset];
            break;
        case 2:
            expected_size = 4;
            host_input = salt_test_data.m4;
            break;
        case 3:
            offset = 4;
            expected_size = sizeof(salt_test_data.m4) - offset;
            host_input = &salt_test_data.m4[offset];
            break;
        case 4:
            expected_size = 4;
            host_input = salt_test_data.msg2;
            break;
        case 5:
            offset = 4;
            expected_size = sizeof(salt_test_data.msg1) - offset;
            host_input = &salt_test_data.msg1[offset];
            break;
    }

    assert(expected_size == p_rchannel->size_expected);
    memcpy(p_rchannel->p_data, host_input, expected_size);
    
    p_rchannel->size = expected_size;
    n++;

    return SALT_SUCCESS;

}

void my_time_impl(uint32_t *p_time) {
    memset(p_time, 0, 4);
}

int main(void)
{

    printf("=== Salt v2 host handshake and echo test begin ===\r\n");

    salt_channel_t channel;
    salt_ret_t ret;
    uint8_t hndsk_buffer[SALT_HNDSHK_BUFFER_SIZE];
    memset(hndsk_buffer, 0xcc, SALT_HNDSHK_BUFFER_SIZE);

    ret = salt_create(&channel, SALT_SERVER, my_write, my_read, my_time_impl);
    ret = salt_set_signature(&channel, salt_test_data.host_sk_sec);
    ret = salt_init_session(&channel, hndsk_buffer, SALT_HNDSHK_BUFFER_SIZE);
    ret = salt_handshake(&channel);
    assert(ret == SALT_SUCCESS);

    /* Check that we did not overflow handshake buffer */
    for (uint32_t i = SALT_HNDSHK_BUFFER_SIZE; i < sizeof(hndsk_buffer); i++)
    {
        assert(hndsk_buffer[i] == 0xcc);
    }

    /* Write echo bytes 010505050505 */
    uint8_t echo_bytes[6] = {0x01, 0x05, 0x05, 0x05, 0x05, 0x05};
    memcpy(&hndsk_buffer[SALT_OVERHEAD_SIZE], echo_bytes, sizeof(echo_bytes));
    ret = salt_write(&channel, hndsk_buffer, SALT_OVERHEAD_SIZE + sizeof(echo_bytes));
    assert(ret == SALT_SUCCESS);

    uint32_t size;
    ret = salt_read(&channel, hndsk_buffer, &size, sizeof(hndsk_buffer));
    assert(ret == SALT_SUCCESS);
    assert(size == sizeof(echo_bytes));
    assert(memcmp(echo_bytes, &hndsk_buffer[SALT_OVERHEAD_SIZE], sizeof(echo_bytes)) == 0);

    printf("=== Salt v2 host handshake and echo test succeeded ===\r\n");

    return 0;
}
