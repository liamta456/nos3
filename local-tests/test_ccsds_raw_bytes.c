#include <string.h>
#include "utassert.h"
#include "uttest.h"
#include "cfe_msg.h"

void Test_APID_NoTruncation(void)
{
    CFE_MSG_Message_t msg;
    memset(&msg, 0, sizeof(msg));

    CFE_MSG_SetApId(&msg, 0x7FD);
    uint16 apid_bytes = ((msg.Byte[0] << 8) | msg.Byte[1]) & 0x07FF;

    UtAssert_INT32_EQ(apid_bytes, 0x7FD);
}

void Test_APID_RangeChecking(void)
{
    CFE_MSG_Message_t msg;
    memset(&msg, 0, sizeof(msg));

    int32 result = CFE_MSG_SetApId(&msg, 0x800);

    UtAssert_INT32_EQ(result, CFE_MSG_BAD_ARGUMENT);
}

void Test_Length_Convention(void)
{
    CFE_MSG_Message_t msg;
    memset(&msg, 0, sizeof(msg));

    CFE_MSG_SetSize(&msg, 106);
    uint16 length_bytes = (msg.Byte[4] << 8) | msg.Byte[5];

    UtAssert_INT32_EQ(length_bytes, 99);
}

void UtTest_Setup(void)
{
    UtTest_Add(Test_APID_NoTruncation, NULL, NULL, "Test_APID_NoTruncation");
    UtTest_Add(Test_APID_RangeChecking, NULL, NULL, "Test_APID_RangeChecking");
    UtTest_Add(Test_Length_Convention, NULL, NULL, "Test_Length_Convention");
}
