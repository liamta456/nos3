/*******************************************************************************
** File: payload_if_app.c
**
** Purpose:
**   This file contains the source code for the PAYLOAD_IF application.
**
*******************************************************************************/

/*
** Include Files
*/
#include <arpa/inet.h>
#include "payload_if_app.h"

/*
** Global Data
*/
PAYLOAD_IF_AppData_t PAYLOAD_IF_AppData;

/*
** Application entry point and main process loop
*/
void PAYLOAD_IF_AppMain(void)
{
    int32 status = OS_SUCCESS;

    /*
    ** Create the first Performance Log entry
    */
    CFE_ES_PerfLogEntry(PAYLOAD_IF_PERF_ID);

    /*
    ** Perform application initialization
    */
    status = PAYLOAD_IF_AppInit();
    if (status != CFE_SUCCESS)
    {
        PAYLOAD_IF_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
    }

    /*
    ** Main loop
    */
    while (CFE_ES_RunLoop(&PAYLOAD_IF_AppData.RunStatus) == true)
    {
        /*
        ** Performance log exit stamp
        */
        CFE_ES_PerfLogExit(PAYLOAD_IF_PERF_ID);

        /*
        ** Pend on the arrival of the next Software Bus message
        ** Note that this is the standard, but timeouts are available
        */
        status = CFE_SB_ReceiveBuffer((CFE_SB_Buffer_t **)&PAYLOAD_IF_AppData.MsgPtr, PAYLOAD_IF_AppData.CmdPipe,
                                      CFE_SB_PEND_FOREVER);

        /*
        ** Begin performance metrics on anything after this line. This will help to determine
        ** where we are spending most of the time during this app execution.
        */
        CFE_ES_PerfLogEntry(PAYLOAD_IF_PERF_ID);

        /*
        ** If the CFE_SB_ReceiveBuffer was successful, then continue to process the command packet
        ** If not, then exit the application in error.
        ** Note that a SB read error should not always result in an app quitting.
        */
        if (status == CFE_SUCCESS)
        {
            PAYLOAD_IF_ProcessCommandPacket();
        }
        else
        {
            CFE_EVS_SendEvent(PAYLOAD_IF_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "PAYLOAD_IF: SB Pipe Read Error = %d",
                              (int)status);
            PAYLOAD_IF_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
        }
    }

    /*
    ** Disable component, which cleans up the interface, upon exit
    */
    PAYLOAD_IF_Disable();

    /*
    ** Performance log exit stamp
    */
    CFE_ES_PerfLogExit(PAYLOAD_IF_PERF_ID);

    /*
    ** Exit the application
    */
    CFE_ES_ExitApp(PAYLOAD_IF_AppData.RunStatus);
}

/*
** Initialize application
*/
int32 PAYLOAD_IF_AppInit(void)
{
    int32 status = OS_SUCCESS;

    PAYLOAD_IF_AppData.RunStatus = CFE_ES_RunStatus_APP_RUN;

    /*
    ** Register the events
    */
    status = CFE_EVS_Register(NULL, 0, CFE_EVS_EventFilter_BINARY); /* as default, no filters are used */
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("PAYLOAD_IF: Error registering for event services: 0x%08X\n", (unsigned int)status);
        return status;
    }

    /*
    ** Create the Software Bus command pipe
    */
    status = CFE_SB_CreatePipe(&PAYLOAD_IF_AppData.CmdPipe, PAYLOAD_IF_PIPE_DEPTH, "PAYLOAD_IF_CMD_PIPE");
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(PAYLOAD_IF_PIPE_ERR_EID, CFE_EVS_EventType_ERROR, "Error Creating SB Pipe,RC=0x%08X",
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to ground commands
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(PAYLOAD_IF_CMD_MID), PAYLOAD_IF_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(PAYLOAD_IF_SUB_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Gnd Cmds, MID=0x%04X, RC=0x%08X", PAYLOAD_IF_CMD_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to housekeeping (hk) message requests
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(PAYLOAD_IF_REQ_HK_MID), PAYLOAD_IF_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(PAYLOAD_IF_SUB_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Request, MID=0x%04X, RC=0x%08X", PAYLOAD_IF_REQ_HK_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Subscribe to MGR HK for Science Pass Information
    ** TODO: This is specific to the payload_if application, remove if using template generator
    */
    status = CFE_SB_Subscribe(CFE_SB_ValueToMsgId(MGR_HK_TLM_MID), PAYLOAD_IF_AppData.CmdPipe);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(PAYLOAD_IF_SUB_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Error Subscribing to HK Request, MID=0x%04X, RC=0x%08X", MGR_HK_TLM_MID,
                          (unsigned int)status);
        return status;
    }

    /*
    ** Initialize the payload-link decoder state and start the asynchronous
    ** UART receive task. Decoder state must persist for the app's lifetime.
    */
    plframe_decode_init(&PAYLOAD_IF_AppData.DecodeCtx);
    PAYLOAD_IF_AppData.RxTaskRunning = true;

    status = CFE_ES_CreateChildTask(&PAYLOAD_IF_AppData.RxTaskID, PAYLOAD_IF_RX_TASK_NAME, PAYLOAD_IF_RxTask,
                                    0, PAYLOAD_IF_RX_TASK_STACK_SIZE, PAYLOAD_IF_RX_TASK_PRIORITY, 0);
    if (status != CFE_SUCCESS)
    {
        CFE_EVS_SendEvent(PAYLOAD_IF_STARTUP_INF_EID, CFE_EVS_EventType_ERROR,
                          "PAYLOAD_IF: Error creating RX task, RC=0x%08X", (unsigned int)status);
        return status;
    }

    /*
    ** TODO: Subscribe to any other messages here
    */

    /*
    ** Initialize the published HK message - this HK message will contain the
    ** telemetry that has been defined in the PAYLOAD_IF_HkTelemetryPkt for this app.
    */
    CFE_MSG_Init(CFE_MSG_PTR(PAYLOAD_IF_AppData.HkTelemetryPkt.TlmHeader), CFE_SB_ValueToMsgId(PAYLOAD_IF_HK_TLM_MID),
                 PAYLOAD_IF_HK_TLM_LNGTH);

    /*
    ** Initialize the device packet message
    ** This packet is specific to your application
    */
    CFE_MSG_Init(CFE_MSG_PTR(PAYLOAD_IF_AppData.DevicePkt.TlmHeader), CFE_SB_ValueToMsgId(PAYLOAD_IF_DEVICE_TLM_MID),
                 PAYLOAD_IF_DEVICE_TLM_LNGTH);

    /*
    ** TODO: Initialize any other messages that this app will publish
    */

    /*
    ** Always reset all counters during application initialization
    */
    PAYLOAD_IF_ResetCounters();

    /*
    ** Initialize application data
    ** Note that counters are excluded as they were reset in the previous code block
    */
    PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceEnabled          = PAYLOAD_IF_DEVICE_DISABLED;
    PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceHK.DeviceCounter = 0;
    PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceHK.DeviceConfig  = 0;
    PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceHK.DeviceStatus  = 0;

    /*
     ** Send an information event that the app has initialized.
     ** This is useful for debugging the loading of individual applications.
     */
    status = CFE_EVS_SendEvent(PAYLOAD_IF_STARTUP_INF_EID, CFE_EVS_EventType_INFORMATION,
                               "PAYLOAD_IF App Initialized. Version %d.%d.%d.%d", PAYLOAD_IF_MAJOR_VERSION,
                               PAYLOAD_IF_MINOR_VERSION, PAYLOAD_IF_REVISION, PAYLOAD_IF_MISSION_REV);
    if (status != CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("PAYLOAD_IF: Error sending initialization event: 0x%08X\n", (unsigned int)status);
    }
    return status;
}

/*
** Process packets received on the PAYLOAD_IF command pipe
*/
void PAYLOAD_IF_ProcessCommandPacket(void)
{
    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_GetMsgId(PAYLOAD_IF_AppData.MsgPtr, &MsgId);
    switch (CFE_SB_MsgIdToValue(MsgId))
    {
        /*
        ** Ground Commands with command codes fall under the PAYLOAD_IF_CMD_MID (Message ID)
        */
        case PAYLOAD_IF_CMD_MID:
            PAYLOAD_IF_ProcessGroundCommand();
            break;

        /*
        ** Housekeeping requests with command codes fall under the PAYLOAD_IF_REQ_HK_MID (Message ID)
        */
        case PAYLOAD_IF_REQ_HK_MID:
            PAYLOAD_IF_ProcessTelemetryRequest();
            break;

        /*
        ** Update science pass information
        ** TODO: This is specific to the payload_if application, remove if using template generator
        */
        case MGR_HK_TLM_MID:
            PAYLOAD_IF_ProcessMgrHk();
            break;

        /*
        ** TODO: Add additional message IDs as needed
        */

        /*
        ** All other invalid messages that this app doesn't recognize,
        ** increment the command error counter and log as an error event.
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command packet */
            PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send event failure to the console*/
            CFE_EVS_SendEvent(PAYLOAD_IF_PROCESS_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Invalid command packet, MID = 0x%x", CFE_SB_MsgIdToValue(MsgId));
            break;
    }
    return;
}

/*
** Process ground commands
** TODO: Add additional commands required by the specific component
*/
void PAYLOAD_IF_ProcessGroundCommand(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /*
    ** MsgId is only needed if the command code is not recognized. See default case
    */
    CFE_MSG_GetMsgId(PAYLOAD_IF_AppData.MsgPtr, &MsgId);

    /*
    ** Ground Commands have a command code (_CC) associated with them
    ** Pull this command code from the message and then process
    */
    CFE_MSG_GetFcnCode(PAYLOAD_IF_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        /*
        ** NOOP Command
        */
        case PAYLOAD_IF_NOOP_CC:
            /*
            ** Verify the command length immediately after CC identification
            */
            if (PAYLOAD_IF_VerifyCmdLength(PAYLOAD_IF_AppData.MsgPtr, sizeof(PAYLOAD_IF_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef PAYLOAD_IF_CFG_DEBUG
                OS_printf("PAYLOAD_IF: PAYLOAD_IF_NOOP_CC received \n");
#endif

                /* Do any necessary checks, none for a NOOP */

                /* Increment command success or error counter, NOOP can only be successful */
                PAYLOAD_IF_AppData.HkTelemetryPkt.CommandCount++;

                /* Do the action, none for a NOOP */

                /* Increment device success or error counter, none for NOOP as application only */

                /* Send event success or failure to the console, NOOP can only be successful */
                CFE_EVS_SendEvent(PAYLOAD_IF_CMD_NOOP_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "PAYLOAD_IF: NOOP command received");
            }
            break;

        /*
        ** Reset Counters Command
        */
        case PAYLOAD_IF_RESET_COUNTERS_CC:
            if (PAYLOAD_IF_VerifyCmdLength(PAYLOAD_IF_AppData.MsgPtr, sizeof(PAYLOAD_IF_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef PAYLOAD_IF_CFG_DEBUG
                OS_printf("PAYLOAD_IF: PAYLOAD_IF_RESET_COUNTERS_CC received \n");
#endif
                PAYLOAD_IF_ResetCounters();
            }
            break;

        /*
        ** Enable Command
        */
        case PAYLOAD_IF_ENABLE_CC:
            if (PAYLOAD_IF_VerifyCmdLength(PAYLOAD_IF_AppData.MsgPtr, sizeof(PAYLOAD_IF_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef PAYLOAD_IF_CFG_DEBUG
                OS_printf("PAYLOAD_IF: PAYLOAD_IF_ENABLE_CC received \n");
#endif
                PAYLOAD_IF_Enable();
            }
            break;

        /*
        ** Disable Command
        */
        case PAYLOAD_IF_DISABLE_CC:
            if (PAYLOAD_IF_VerifyCmdLength(PAYLOAD_IF_AppData.MsgPtr, sizeof(PAYLOAD_IF_NoArgs_cmd_t)) == OS_SUCCESS)
            {
#ifdef PAYLOAD_IF_CFG_DEBUG
                OS_printf("PAYLOAD_IF: PAYLOAD_IF_DISABLE_CC received \n");
#endif
                PAYLOAD_IF_Disable();
            }
            break;

        /*
        ** Set Configuration Command
        ** Note that this is an example of a command that has additional arguments
        */
        case PAYLOAD_IF_CONFIG_CC:
            if (PAYLOAD_IF_VerifyCmdLength(PAYLOAD_IF_AppData.MsgPtr, sizeof(PAYLOAD_IF_Config_cmd_t)) == OS_SUCCESS)
            {
#ifdef PAYLOAD_IF_CFG_DEBUG
                OS_printf("PAYLOAD_IF: PAYLOAD_IF_CONFIG_CC received \n");
#endif
                PAYLOAD_IF_Configure();
            }
            break;

        /*
        ** TODO: Edit and add more command codes as appropriate for the application
        */

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the command error counter upon receipt of an invalid command */
            PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_CMD_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Process Telemetry Request - Triggered in response to a telemetry request
*/
void PAYLOAD_IF_ProcessTelemetryRequest(void)
{
    CFE_SB_MsgId_t    MsgId       = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t CommandCode = 0;

    /* MsgId is only needed if the command code is not recognized. See default case */
    CFE_MSG_GetMsgId(PAYLOAD_IF_AppData.MsgPtr, &MsgId);

    /* Pull this command code from the message and then process */
    CFE_MSG_GetFcnCode(PAYLOAD_IF_AppData.MsgPtr, &CommandCode);
    switch (CommandCode)
    {
        case PAYLOAD_IF_REQ_HK_TLM:
            PAYLOAD_IF_ReportHousekeeping();
            break;

        case PAYLOAD_IF_REQ_DATA_TLM:
            PAYLOAD_IF_ReportDeviceTelemetry();
            break;

        /*
        ** TODO: Edit, add, or remove telemetry request codes appropriate for the application
        */

        /*
        ** Invalid Command Codes
        */
        default:
            /* Increment the error counter upon receipt of an invalid command */
            PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount++;

            /* Send invalid command code failure to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_DEVICE_TLM_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Invalid command code for packet, MID = 0x%x, cmdCode = 0x%x",
                              CFE_SB_MsgIdToValue(MsgId), CommandCode);
            break;
    }
    return;
}

/*
** Report Application Housekeeping
*/
void PAYLOAD_IF_ReportHousekeeping(void)
{
    int32 status = OS_SUCCESS;

    /* Check that device is enabled */
    if (PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceEnabled == PAYLOAD_IF_DEVICE_ENABLED)
    {
        status = PAYLOAD_IF_RequestHK(&PAYLOAD_IF_AppData.Payload_ifUart,
                                  (PAYLOAD_IF_Device_HK_tlm_t *)&PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceHK);
        if (status == OS_SUCCESS)
        {
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceCount++;
        }
        else
        {
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(PAYLOAD_IF_REQ_HK_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Request device HK reported error %d", status);
        }
    }
    /* Intentionally do not report errors if disabled */

    /* Time stamp and publish housekeeping telemetry */
    CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&PAYLOAD_IF_AppData.HkTelemetryPkt);
    CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&PAYLOAD_IF_AppData.HkTelemetryPkt, true);
    return;
}

/*
** Collect and Report Device Telemetry
*/
void PAYLOAD_IF_ReportDeviceTelemetry(void)
{
    int32 status = OS_SUCCESS;

    /* Check that device is enabled */
    if (PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceEnabled == PAYLOAD_IF_DEVICE_ENABLED)
    {
        status = PAYLOAD_IF_RequestData(&PAYLOAD_IF_AppData.Payload_ifUart,
                                    (PAYLOAD_IF_Device_Data_tlm_t *)&PAYLOAD_IF_AppData.DevicePkt.Payload_if);
        if (status == OS_SUCCESS)
        {
            /* Update packet count */
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceCount++;

            /* Time stamp and publish data telemetry */
            CFE_SB_TimeStampMsg((CFE_MSG_Message_t *)&PAYLOAD_IF_AppData.DevicePkt);
            CFE_SB_TransmitMsg((CFE_MSG_Message_t *)&PAYLOAD_IF_AppData.DevicePkt, true);
        }
        else
        {
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceErrorCount++;
            CFE_EVS_SendEvent(PAYLOAD_IF_REQ_DATA_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Request device data reported error %d", status);
        }

        /* Check device status and act on error */
        if (PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceHK.DeviceStatus != 0)
        {
            /* Any bit is an error, halting communication until device power cycled */
            PAYLOAD_IF_Disable();

            /* Send device status error to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_REQ_DATA_STATUS_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Request device data reported status error %d",
                              PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceHK.DeviceStatus);
        }
    }
    /* Intentionally do not report errors if device disabled */
    return;
}

/*
** Ingest science MGR data and save it
** TODO: This is specific to the payload_if application, remove if using template generator
*/
void PAYLOAD_IF_ProcessMgrHk(void)
{
    MGR_Hk_tlm_t *pMsg = (MGR_Hk_tlm_t *)PAYLOAD_IF_AppData.MsgPtr;

    PAYLOAD_IF_AppData.DevicePkt.PassNumber   = pMsg->SciPassCount;
    PAYLOAD_IF_AppData.DevicePkt.RegionStatus = pMsg->ScienceStatus;
    return;
}

/*
** Reset all global counter variables
*/
void PAYLOAD_IF_ResetCounters(void)
{
    /* Do any necessary checks, none for reset counters */

    /* Increment command success or error counter, omitted as action is to reset */

    /* Do the action, clear all global counter variables */
    PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount = 0;
    PAYLOAD_IF_AppData.HkTelemetryPkt.CommandCount      = 0;
    PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceErrorCount  = 0;
    PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceCount       = 0;

    /* Increment device success or error counter, none as application only */

    /* Send event success to the console */
    CFE_EVS_SendEvent(PAYLOAD_IF_CMD_RESET_INF_EID, CFE_EVS_EventType_INFORMATION,
                      "PAYLOAD_IF: RESET counters command received");
    return;
}

/*
** Enable Component
** TODO: Edit for your specific component implementation
*/
void PAYLOAD_IF_Enable(void)
{
    int32 status = OS_SUCCESS;

    /* Do any necessary checks, confirm that device is currently disabled */
    if (PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceEnabled == PAYLOAD_IF_DEVICE_DISABLED)
    {
        /* Increment command success counter */
        PAYLOAD_IF_AppData.HkTelemetryPkt.CommandCount++;

        /*
        ** Do the action, initialize hardware interface and set enabled
        ** TODO: Make specific to your application depending on protocol in use
        ** Note that other components provide examples for the different protocols
        */
        PAYLOAD_IF_AppData.Payload_ifUart.deviceString  = PAYLOAD_IF_CFG_STRING;
        PAYLOAD_IF_AppData.Payload_ifUart.handle        = PAYLOAD_IF_CFG_HANDLE;
        PAYLOAD_IF_AppData.Payload_ifUart.isOpen        = PORT_CLOSED;
        PAYLOAD_IF_AppData.Payload_ifUart.baud          = PAYLOAD_IF_CFG_BAUDRATE_HZ;
        PAYLOAD_IF_AppData.Payload_ifUart.access_option = uart_access_flag_RDWR;

        status = uart_init_port(&PAYLOAD_IF_AppData.Payload_ifUart);
        if (status == OS_SUCCESS)
        {
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceEnabled = PAYLOAD_IF_DEVICE_ENABLED;

            /* Increment device success counter */
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_ENABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "PAYLOAD_IF: Device enabled successfully");
        }
        else
        {
            /* Increment device error counter */
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_UART_INIT_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Device UART port initialization error %d", status);
        }
    }
    else
    {
        /* Increment command error count */
        PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send command event failure to the console */
        CFE_EVS_SendEvent(PAYLOAD_IF_ENABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PAYLOAD_IF: Device enable failed, already enabled");
    }
    return;
}

/*
** Disable Component
** TODO: Edit for your specific component implementation
*/
void PAYLOAD_IF_Disable(void)
{
    int32 status = OS_SUCCESS;

    /* Do any necessary checks, confirm that device is currently enabled */
    if (PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceEnabled == PAYLOAD_IF_DEVICE_ENABLED)
    {
        /* Increment command success counter */
        PAYLOAD_IF_AppData.HkTelemetryPkt.CommandCount++;

        /*
        ** Do the action, close hardware interface and set disabled
        ** TODO: Make specific to your application depending on protocol in use
        ** Note that other components provide examples for the different protocols
        */
        status = uart_close_port(&PAYLOAD_IF_AppData.Payload_ifUart);
        if (status == OS_SUCCESS)
        {
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceEnabled = PAYLOAD_IF_DEVICE_DISABLED;

            /* Increment device success counter */
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_DISABLE_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "PAYLOAD_IF: Device disabled successfully");
        }
        else
        {
            /* Increment device error counter */
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_UART_CLOSE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Device UART port close error %d", status);
        }
    }
    else
    {
        /* Increment command error count */
        PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send command event failure to the console */
        CFE_EVS_SendEvent(PAYLOAD_IF_DISABLE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PAYLOAD_IF: Device disable failed, already disabled");
    }
    return;
}

/*
** Configure Component
** TODO: Edit for your specific component implementation
*/
void PAYLOAD_IF_Configure(void)
{
    int32                status        = OS_SUCCESS;
    int32                device_status = OS_SUCCESS;
    PAYLOAD_IF_Config_cmd_t *config_cmd    = (PAYLOAD_IF_Config_cmd_t *)PAYLOAD_IF_AppData.MsgPtr;

    /* Do any necessary checks, confirm that device is currently enabled */
    if (PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceEnabled != PAYLOAD_IF_DEVICE_ENABLED)
    {
        status = OS_ERROR;
        /* Increment command error count */
        PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send event logging failure of check to the console */
        CFE_EVS_SendEvent(PAYLOAD_IF_CMD_CONFIG_EN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PAYLOAD_IF: Configuration command invalid when device disabled");
    }

    /* Do any necessary checks, confirm valid configuration value */
    if (config_cmd->DeviceCfg == 0xFFFFFFFF) // 4294967295
    {
        status = OS_ERROR;
        /* Increment command error count */
        PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount++;

        /* Send event logging failure of check to the console */
        CFE_EVS_SendEvent(PAYLOAD_IF_CMD_CONFIG_VAL_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PAYLOAD_IF: Configuration command with value %u is invalid", config_cmd->DeviceCfg);
    }

    if (status == OS_SUCCESS)
    {
        /* Increment command success counter */
        PAYLOAD_IF_AppData.HkTelemetryPkt.CommandCount++;

        /* Do the action, command device to with a new configuration */
        device_status = PAYLOAD_IF_CommandDevice(&PAYLOAD_IF_AppData.Payload_ifUart, PAYLOAD_IF_DEVICE_CFG_CMD, config_cmd->DeviceCfg);
        if (device_status == OS_SUCCESS)
        {
            /* Increment device success counter */
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceCount++;

            /* Send device event success to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_CMD_CONFIG_INF_EID, CFE_EVS_EventType_INFORMATION,
                              "PAYLOAD_IF: Configuration command received: %u", config_cmd->DeviceCfg);
        }
        else
        {
            /* Increment device error counter */
            PAYLOAD_IF_AppData.HkTelemetryPkt.DeviceErrorCount++;

            /* Send device event failure to the console */
            CFE_EVS_SendEvent(PAYLOAD_IF_CMD_CONFIG_DEV_ERR_EID, CFE_EVS_EventType_ERROR,
                              "PAYLOAD_IF: Configuration command received: %u", config_cmd->DeviceCfg);
        }
    }
    return;
}

/*
** Verify command packet length matches expected
*/
/*
** Asynchronous UART receive task
** Continuously polls for available UART bytes and feeds them into the
** persistent payload-link decoder. Runs until RxTaskRunning is cleared
** by PAYLOAD_IF_Disable.
*/
void PAYLOAD_IF_RxTask(void)
{
    uint8_t byte;
    int32   bytes_available;
    int32   bytes_read;

    while (PAYLOAD_IF_AppData.RxTaskRunning)
    {
        bytes_available = uart_bytes_available(&PAYLOAD_IF_AppData.Payload_ifUart);

        if (bytes_available > 0)
        {
            bytes_read = uart_read_port(&PAYLOAD_IF_AppData.Payload_ifUart, &byte, 1);

            if (bytes_read == 1)
            {
                plframe_decode_result_t result = plframe_decode_feed(&PAYLOAD_IF_AppData.DecodeCtx, byte);

                switch (result)
                {
                    case PL_DECODE_OK:
                        /* TODO: validate CCSDS header/length/allowlist, then publish */
                        break;

                    case PL_DECODE_BAD_CRC:
                        /* TODO: increment bad CRC counter */
                        break;

                    case PL_DECODE_RESYNC:
                        /* TODO: increment resync counter */
                        break;

                    case PL_DECODE_NEED_MORE:
                    default:
                        break;
                }
            }
        }

        OS_TaskDelay(PAYLOAD_IF_RX_TASK_MS_DELAY);
    }
}

int32 PAYLOAD_IF_VerifyCmdLength(CFE_MSG_Message_t *msg, uint16 expected_length)
{
    int32             status        = OS_SUCCESS;
    CFE_SB_MsgId_t    msg_id        = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t cmd_code      = 0;
    size_t            actual_length = 0;

    CFE_MSG_GetSize(msg, &actual_length);
    if (expected_length != actual_length)
    {
        CFE_MSG_GetMsgId(msg, &msg_id);
        CFE_MSG_GetFcnCode(msg, &cmd_code);

        CFE_EVS_SendEvent(PAYLOAD_IF_LEN_ERR_EID, CFE_EVS_EventType_ERROR,
                          "Invalid msg length: ID = 0x%X,  CC = %d, Len = %ld, Expected = %d",
                          CFE_SB_MsgIdToValue(msg_id), cmd_code, actual_length, expected_length);

        status = OS_ERROR;

        /* Increment the command error counter upon receipt of an invalid command length */
        PAYLOAD_IF_AppData.HkTelemetryPkt.CommandErrorCount++;
    }
    return status;
}
