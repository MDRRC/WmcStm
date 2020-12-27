/**
 **********************************************************************************************************************
 * @file  wmc_app.h
 * @brief Main application of the Wifi manual control.
 ***********************************************************************************************************************
 */
#ifndef WMC_APP_H
#define WMC_APP_H

/***********************************************************************************************************************
 * I N C L U D E S
 **********************************************************************************************************************/
#include "LocStorage.h"
#include "Loclib.h"
#include "WmcCli.h"
#include "WmcTft.h"
#include "Z21Slave.h"
#include "wmc_event.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <tinyfsm.hpp>

/***********************************************************************************************************************
 * T Y P E D  E F S  /  E N U M
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * C L A S S E S
 **********************************************************************************************************************/

class wmcApp : public tinyfsm::Fsm<wmcApp>
{
public:
    /* default reaction for unhandled events */
    void react(tinyfsm::Event const&){};

    virtual void react(cvProgEvent const&);
    virtual void react(cliEnterEvent const&);
    virtual void react(updateEvent3sec const&);
    virtual void react(pushButtonsEvent const&);
    virtual void react(pulseSwitchEvent const&);
    virtual void react(updateEvent5msec const&);
    virtual void react(updateEvent50msec const&);
    virtual void react(updateEvent100msec const&);
    virtual void react(updateEvent500msec const&);

    virtual void entry(void){}; /* entry actions in some states */
    virtual void exit(void){};  /* no exit actions at all */

    enum powerState
    {
        on = 0,
        off,
        emergency
    };

protected:
    Z21Slave::dataType WmcCheckForDataRx(void);
    void WmcCheckForDataTx(void);
    void convertLocDataToDisplayData(Z21Slave::locInfo* Z21DataPtr, WmcTft::locoInfo* TftDataPtr);
    bool updateLocInfoOnScreen(bool updateAll);
    void PrepareLanXSetLocoDriveAndTransmit(uint16_t Speed);
    int8_t CheckPulseSwitchRevert(int8_t Delta);

    static const uint8_t CONNECT_CNT_MAX_FAIL_CONNECT_WIFI = 200;
    static const uint8_t CONNECT_CNT_MAX_FAIL_CONNECT_UDP  = 40;
    static const uint16_t ADDRESS_TURNOUT_MIN              = 1;
    static const uint16_t ADDRESS_TURNOUT_MAX              = 9999;
    static const uint8_t FUNCTION_MIN                      = 0;
    static const uint8_t FUNCTION_MAX                      = 28;
    static const uint8_t ADC_VALUES_ARRAY_SIZE             = 7;
    static const uint8_t ADC_VALUES_ARRAY_REFERENCE_INDEX  = 6;

    static WmcTft m_wmcTft;
    static LocLib m_locLib;
    static WiFiUDP m_WifiUdp;
    static WmcCli m_WmcCommandLine;
    static LocStorage m_LocStorage;
    static wmcApp::powerState m_TrackPower;
    static Z21Slave m_z21Slave;
    static bool m_locSelection;
    static uint16_t m_ConnectCnt;
    static uint8_t m_IpAddresZ21[4];
    static uint8_t m_IpAddresWmc[4];
    static uint8_t m_IpGateway[4];
    static uint8_t m_IpSubnet[4];
    static uint16_t m_UdpLocalPort;
    static uint16_t m_locAddressAdd;
    static uint16_t m_TurnOutAddress;
    static Z21Slave::turnout m_TurnOutDirection;
    static uint32_t m_TurnoutOffDelay;
    static uint16_t m_locAddressChange;
    static uint16_t m_locAddressChangeActive;
    static uint16_t m_locDbDataTransmitCnt;
    static uint32_t m_locDbDataTransmitCntRepeat;
    static uint16_t m_locAddressDelete;
    static uint16_t m_LocAddresActualDelete;
    static byte m_WmcPacketBuffer[40];
    static uint8_t m_locFunctionAdd;
    static uint8_t m_locFunctionChange;
    static uint16_t m_LocInfoRequestCounter;
    static uint8_t m_locFunctionAssignment[5];
    static Z21Slave::locInfo m_WmcLocInfoControl;
    static Z21Slave::locInfo* m_WmcLocInfoReceived;
    static Z21Slave::locLibData* m_WmcLocLibInfo;
    static bool m_ButtonPrevious;
    static bool m_PulseSwitchInvert;
    static bool m_TurnoutAutoOff;
    static uint8_t m_ButtonIndexPrevious;
    static bool m_WmcLocSpeedRequestPending;
    static bool m_CvPomProgramming;
    static bool m_CvPomProgrammingFromPowerOn;
    static bool m_EmergencyStopEnabled;

    static uint16_t m_AdcButtonValue[ADC_VALUES_ARRAY_SIZE];
    static uint16_t m_AdcButtonValuePrevious;
    static uint8_t m_AdcIndex;

    static pushButtonsEvent m_wmcPushButtonEvent;

    static const uint32_t LOC_DATABASE_TX_DELAY = 200;
};

#endif
