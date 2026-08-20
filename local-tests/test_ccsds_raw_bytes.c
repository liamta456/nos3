#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "cfe_msg.h"

int main(void) {
    CFE_MSG_Message_t msg;

    /* Zero out the message struct first */
    memset(&msg, 0, sizeof(msg));

    /* Check 1: APID must not truncate */
    CFE_MSG_SetApId(&msg, 0x7FD);
    uint16_t apid_bytes = ((msg.Byte[0] << 8) | msg.Byte[1]) & 0x07FF;
    assert(apid_bytes == 0x7FD);
    printf("PASS: APID round-trips as 0x%X\n", apid_bytes);

    /* Check 2: Length must be size - 7 */
    CFE_MSG_SetSize(&msg, 106);
    uint16_t length_bytes = (msg.Byte[4] << 8) | msg.Byte[5];
    assert(length_bytes == 99);
    printf("PASS: Length encodes as %d (expected 99)\n", length_bytes);

    printf("All checks passed.\n");
    return 0;
}
