/***********************************************************************************************************************
   @file   wc_app.cpp
   @brief  Main application of WifiManualControl (WMC).
 **********************************************************************************************************************/

/***********************************************************************************************************************
   I N C L U D E S
 **********************************************************************************************************************/
#include "wmc_app.h"
#include "eep_cfg.h"
#include "fsmlist.hpp"
#include "user_interface.h"
#include "version.h"
#include "wmc_cv.h"
#include "wmc_event.h"
#include <EEPROM.h>
#include <tinyfsm.hpp>

/***********************************************************************************************************************
   D E F I N E S
 **********************************************************************************************************************/
#define WMC_APP_DEBUG_TX_RX 0
#define WMC_APP_ANALOG_IN A0
/***********************************************************************************************************************
   F O R W A R D  D E C L A R A T I O N S
 **********************************************************************************************************************/
class stateInit;
class stateSetUpWifi;
class stateInitUdpConnect;
class stateInitUdpConnectFail;
class stateAdcButtons;
class stateSetUpWifiFail;
class stateInitBroadcast;
class stateInitStatusGet;
class stateInitLocInfoGet;
class statePowerOff;
class statePowerOn;
class stateEmergencyStop;
class statePowerProgrammingMode;
class stateTurnoutControl;
class stateTurnoutControlPowerOff;
class stateMainMenu1;
class stateMainMenu2;
class stateMenuTransmitLocDatabase;
class stateMenuLocAdd;
class stateMenuLocFunctionsAdd;
class stateMenuLocFunctionsChange;
class stateMenuLocDelete;
class stateCommandLineInterfaceActive;
class stateCvProgramming;

/***********************************************************************************************************************
   D A T A   D E C L A R A T I O N S (exported, local)
 **********************************************************************************************************************/

/* Init variables. */
WmcTft wmcApp::m_wmcTft;
LocLib wmcApp::m_locLib;
WiFiUDP wmcApp::m_WifiUdp;
Z21Slave wmcApp::m_z21Slave;
WmcCli wmcApp::m_WmcCommandLine;
LocStorage wmcApp::m_LocStorage;
bool wmcApp::m_locSelection;
uint8_t wmcApp::m_IpAddresZ21[4];
uint8_t wmcApp::m_IpAddresWmc[4];
uint8_t wmcApp::m_IpGateway[4];
uint8_t wmcApp::m_IpSubnet[4];
byte wmcApp::m_WmcPacketBuffer[40];
wmcApp::powerState wmcApp::m_TrackPower       = powerState::off;
uint16_t wmcApp::m_ConnectCnt                 = 0;
uint16_t wmcApp::m_UdpLocalPort               = 21105;
uint16_t wmcApp::m_locAddressAdd              = 1;
uint16_t wmcApp::m_TurnOutAddress             = ADDRESS_TURNOUT_MIN;
Z21Slave::turnout wmcApp::m_TurnOutDirection  = Z21Slave::directionOff;
uint32_t wmcApp::m_TurnoutOffDelay            = 0;
uint8_t wmcApp::m_locFunctionAdd              = 0;
uint8_t wmcApp::m_locFunctionChange           = 0;
uint16_t wmcApp::m_locAddressDelete           = 0;
uint16_t wmcApp::m_LocAddresActualDelete      = 0;
uint16_t wmcApp::m_locAddressChange           = 0;
uint16_t wmcApp::m_locDbDataTransmitCnt       = 0;
uint32_t wmcApp::m_locDbDataTransmitCntRepeat = 0;
bool wmcApp::m_WmcLocSpeedRequestPending      = false;
bool wmcApp::m_CvPomProgramming               = false;
bool wmcApp::m_CvPomProgrammingFromPowerOn    = false;
bool wmcApp::m_EmergencyStopEnabled           = false;
bool wmcApp::m_ButtonPrevious                 = false;
uint8_t wmcApp::m_ButtonIndexPrevious         = 0;
uint8_t wmcApp::m_AdcIndex                    = 0;
uint16_t wmcApp::m_AdcButtonValuePrevious     = 1024;

uint8_t wmcApp::m_locFunctionAssignment[5];
uint16_t wmcApp::m_AdcButtonValue[ADC_VALUES_ARRAY_SIZE];

pushButtonsEvent wmcApp::m_wmcPushButtonEvent;
Z21Slave::locInfo wmcApp::m_WmcLocInfoControl;
Z21Slave::locInfo* wmcApp::m_WmcLocInfoReceived = NULL;
Z21Slave::locLibData* wmcApp::m_WmcLocLibInfo   = NULL;

/***********************************************************************************************************************
  F U N C T I O N S
 **********************************************************************************************************************/

class stateInit : public wmcApp
{
    void entry() override
    {
        m_wmcTft.Init();
        m_wmcTft.Clear();
        m_LocStorage.Init();
    };

    void react(updateEvent100msec const&) override
    {
        uint8_t buttonAdcValid;

        buttonAdcValid = EEPROM.read(EepCfg::ButtonAdcValuesAddressValid);
        if (buttonAdcValid != 1)
        {
            transit<stateAdcButtons>();
        }
        else
        {
            EEPROM.get(EepCfg::ButtonAdcValuesAddress, m_AdcButtonValue);
            transit<stateSetUpWifi>();
        }
    };
};

/***********************************************************************************************************************
 * Init the wifi connection.
 */
class stateSetUpWifi : public wmcApp
{
    /**
     * Init modules and start connection to wifi network.
     */
    void entry() override
    {
        char SsidName[50];
        char SsidPassword[64];
        uint8_t StaticIp = 0;
        uint8_t Index    = 0;

        memset(SsidName, '\0', sizeof(SsidName));
        memset(SsidPassword, '\0', sizeof(SsidPassword));

        m_ConnectCnt = 0;

        /* Init modules. */
        m_wmcTft.ShowName();
        m_wmcTft.ShowVersion(SW_MAJOR, SW_MINOR, SW_PATCH);
        m_EmergencyStopEnabled = m_LocStorage.EmergencyOptionGet();

        m_locLib.Init(m_LocStorage);
        m_WmcCommandLine.Init(m_locLib, m_LocStorage);
        m_wmcTft.UpdateStatus("CONNECTING TO WIFI", true, WmcTft::color_yellow);
        m_wmcTft.UpdateRunningWheel(m_ConnectCnt);

        /* Get SSID data from EEPROM. */
        EEPROM.get(EepCfg::SsidNameAddress, SsidName);
        EEPROM.get(EepCfg::SsidPasswordAddress, SsidPassword);

        /* Get IP data. */
        EEPROM.get(EepCfg::EepIpSubnet, m_IpSubnet);
        EEPROM.get(EepCfg::EepIpGateway, m_IpGateway);
        EEPROM.get(EepCfg::EepIpAddressWmc, m_IpAddresWmc);
        EEPROM.get(EepCfg::EepIpAddressZ21, m_IpAddresZ21);
        StaticIp = EEPROM.read(EepCfg::StaticIpAddress);

        /* Get ADC button data. */
        for (Index = 0; Index < ADC_VALUES_ARRAY_SIZE; Index++)
        {
            m_AdcButtonValue[Index] = (uint16_t)(EEPROM.read(EepCfg::ButtonAdcValuesAddress + (Index * 2))) << 8;
            m_AdcButtonValue[Index] |= (uint16_t)(EEPROM.read(EepCfg::ButtonAdcValuesAddress + (Index * 2) + 1));
        }

        m_wmcTft.ShowNetworkName(SsidName);

        /* Start wifi connection. */
        WiFi.mode(WIFI_STA);

        /* If static IP active set fixed IP settings for static IP. */
        if (StaticIp == 1)
        {
            IPAddress ip(m_IpAddresWmc[0], m_IpAddresWmc[1], m_IpAddresWmc[2], m_IpAddresWmc[3]);
            IPAddress gateway(m_IpGateway[0], m_IpGateway[1], m_IpGateway[2], m_IpGateway[3]);
            IPAddress subnet(m_IpSubnet[0], m_IpSubnet[1], m_IpSubnet[2], m_IpSubnet[3]);

            WiFi.config(ip, gateway, subnet);
        }

        /* Check for password length, if no password connect with NULL. */
        if (strlen(SsidPassword) == 0)
        {
            WiFi.begin(SsidName, NULL);
        }
        else
        {
            WiFi.begin(SsidName, SsidPassword);
        }
    };

    /**
     * Wait for connection or when no connection can be made enter wifi error
     * state.
     */
    void react(updateEvent500msec const&) override
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            m_ConnectCnt++;
            if (m_ConnectCnt < CONNECT_CNT_MAX_FAIL_CONNECT_WIFI)
            {
                m_wmcTft.UpdateRunningWheel(m_ConnectCnt);
            }
            else
            {
                transit<stateSetUpWifiFail>();
            }
        }
        else
        {
            /* Start UDP */
            transit<stateInitUdpConnect>();
        }
    };

    /**
     * Avoid sending data.
     */
    void react(updateEvent3sec const&) override{};
};

/***********************************************************************************************************************
 * No wifi connection could be made, show error screen.
 */
class stateSetUpWifiFail : public wmcApp
{
    void entry() override { m_wmcTft.WifiConnectFailed(); }

    void react(updateEvent50msec const&) override{};
    void react(updateEvent500msec const&) override{};
    void react(updateEvent3sec const&) override{};
};

/***********************************************************************************************************************
 * Setup udp connection and request status to get communication up and running.
 */
class stateInitUdpConnect : public wmcApp
{
    /**
     * Start UDP connection.
     */
    void entry()
    {
        char IpStr[20];

        snprintf(IpStr, sizeof(IpStr), "%hu.%hu.%hu.%hu", m_IpAddresZ21[0], m_IpAddresZ21[1], m_IpAddresZ21[2],
            m_IpAddresZ21[3]);
        m_ConnectCnt = 0;
        m_wmcTft.ClearNetworkName();
        m_wmcTft.UpdateStatus("CONNECT TO CONTROL", true, WmcTft::color_yellow);

        m_wmcTft.ShowIpAddressToConnectTo(IpStr);
        m_wmcTft.UpdateRunningWheel(m_ConnectCnt);
        m_WifiUdp.begin(m_UdpLocalPort);
    }

    /**
     * Request status to check connection with control.
     */
    void react(updateEvent500msec const&) override
    {
        m_ConnectCnt++;

        if (m_ConnectCnt < CONNECT_CNT_MAX_FAIL_CONNECT_UDP)
        {
            m_z21Slave.LanGetStatus();
            WmcCheckForDataTx();
            m_wmcTft.UpdateRunningWheel(m_ConnectCnt);
        }
        else
        {
            transit<stateInitUdpConnectFail>();
        }
    };

    /**
     * Handle the response on the status message.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOff:
        case Z21Slave::programmingMode:
        case Z21Slave::trackPowerOn: transit<stateInitBroadcast>(); break;
        default: break;
        }
    };

    /**
     * Override update during init.
     */
    void react(updateEvent3sec const&) override{};
};

/***********************************************************************************************************************
 * No UDP connection to control unit possible.
 */
class stateInitUdpConnectFail : public wmcApp
{
    void entry() override { m_wmcTft.UdpConnectFailed(); }

    /**
     * Handle the response on the status message of the 3 seconds update event,
     * control device might be enabled somewhat later.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOff:
        case Z21Slave::programmingMode:
        case Z21Slave::trackPowerOn: transit<stateInitBroadcast>(); break;
        default: break;
        }
    };
};

/***********************************************************************************************************************
 * No UDP connection to control unit possible.
 */
class stateAdcButtons : public wmcApp
{
    void entry() override
    {
        m_AdcIndex = 0;
        m_wmcTft.UpdateStatus("BUTTON ADC LEARN", true, WmcTft::color_yellow);
        m_wmcTft.ShowButtonToPress(m_AdcIndex);

        /* Array item 6 contains the non pressed ADC value. This mat vary, 1024 is
         * expected but lower values are also observed, so store nnon pressed value.
         */
        m_AdcButtonValue[ADC_VALUES_ARRAY_REFERENCE_INDEX] = analogRead(WMC_APP_ANALOG_IN);
        m_AdcButtonValuePrevious                           = m_AdcButtonValue[ADC_VALUES_ARRAY_REFERENCE_INDEX];
    }

    /**
     */
    void react(updateEvent100msec const&) override
    {
        uint8_t Index          = 0;
        uint8_t buttonAdcValid = 0;
        uint16_t AdcValue      = analogRead(WMC_APP_ANALOG_IN);

        if (AdcValue >= (m_AdcButtonValue[ADC_VALUES_ARRAY_REFERENCE_INDEX] - 100))
        {
            if (m_AdcButtonValuePrevious < m_AdcButtonValue[ADC_VALUES_ARRAY_REFERENCE_INDEX] - 100)
            {
                m_AdcButtonValue[m_AdcIndex] = m_AdcButtonValuePrevious;
                m_AdcIndex++;

                if (m_AdcIndex >= 6)
                {
                    // Store all "learned" data.
                    for (Index = 0; Index < ADC_VALUES_ARRAY_SIZE; Index++)
                    {
                        EEPROM.write(
                            EepCfg::ButtonAdcValuesAddress + (Index * 2), (m_AdcButtonValue[Index] >> 8) & 0xFF);
                        EEPROM.write(EepCfg::ButtonAdcValuesAddress + (Index * 2) + 1, m_AdcButtonValue[Index] & 0xFF);
                        EEPROM.commit();
                    }

                    buttonAdcValid = 1;
                    EEPROM.write(EepCfg::ButtonAdcValuesAddressValid, buttonAdcValid);
                    EEPROM.commit();

                    transit<stateSetUpWifi>();
                }
                else
                {
                    m_wmcTft.ShowButtonToPress(m_AdcIndex);
                }

                m_AdcButtonValuePrevious = AdcValue;
            }
        }
        else
        {
            m_AdcButtonValuePrevious = AdcValue;
        }
    };
};

/***********************************************************************************************************************
 * Sent broadcast message.
 */
class stateInitBroadcast : public wmcApp
{
    /**
     * Transmit broadcast info.
     */
    void entry() override
    {
        m_z21Slave.LanSetBroadCastFlags(1);
        WmcCheckForDataTx();
    };

    /**
     * Continue to next state.
     */
    void react(updateEvent50msec const&) override { transit<stateInitStatusGet>(); };

    /**
     * Override update during init.
     */
    void react(updateEvent3sec const&) override{};
};

/***********************************************************************************************************************
 * Get the status of the control unit.
 */
class stateInitStatusGet : public wmcApp
{
    /**
     * Get the status.
     */
    void entry() override
    {
        m_z21Slave.LanGetStatus();
        WmcCheckForDataTx();
    };

    /**
     * Check response of status request.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOff:
            m_TrackPower = powerState::off;
            transit<stateInitLocInfoGet>();
            break;
        case Z21Slave::programmingMode:
            m_TrackPower = powerState::off;
            transit<stateInitLocInfoGet>();
            break;
        case Z21Slave::trackPowerOn:
            m_TrackPower = powerState::on;
            transit<stateInitLocInfoGet>();
            break;
        case Z21Slave::emergencyStop:
            m_TrackPower = powerState::emergency;
            transit<stateInitLocInfoGet>();
            break;
        default: break;
        }
    };

    /**
     * No response, retry.
     */
    void react(updateEvent500msec const&) override { m_z21Slave.LanGetStatus(); };

    /**
     * Override update during init.
     */
    void react(updateEvent3sec const&) override{};
};

/***********************************************************************************************************************
 * Get the data of the actual selected loc.
 */
class stateInitLocInfoGet : public wmcApp
{
    /**
     * Request loc info.
     */
    void entry() override
    {
        /* Get loc data. */
        m_locLib.UpdateLocData(m_locLib.GetActualLocAddress());
        m_z21Slave.LanXGetLocoInfo(m_locLib.GetActualLocAddress());
        WmcCheckForDataTx();
    };

    /**
     * Handle response of loc request and if loc data received setup screen.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::locinfo:
            m_wmcTft.Clear();
            if (updateLocInfoOnScreen(true) == true)
            {
                m_locLib.SpeedUpdate(m_WmcLocInfoReceived->Speed);

                if (m_WmcLocInfoReceived->Direction == Z21Slave::locDirectionForward)
                {
                    m_locLib.DirectionSet(directionForward);
                }
                else
                {
                    m_locLib.DirectionSet(directionBackWard);
                }

                switch (m_TrackPower)
                {
                case powerState::off: transit<statePowerOff>(); break;
                case powerState::on: transit<statePowerOn>(); break;
                case powerState::emergency: transit<stateEmergencyStop>(); break;
                }
            }
            break;
        default: break;
        }
    };

    /**
     * No response, retry.
     */
    void react(updateEvent500msec const&) override
    {
        m_z21Slave.LanXGetLocoInfo(m_locLib.GetActualLocAddress());
        WmcCheckForDataTx();
    }

    /**
     * Override update during init.
     */
    void react(updateEvent3sec const&) override{};
};

/***********************************************************************************************************************
 * Control in power off mode. From here go to power on or menu.
 */
class statePowerOff : public wmcApp
{
    uint8_t Index                    = 0;
    uint8_t locFunctionAssignment[5] = { 0, 1, 2, 3, 4 };

    /**
     * Update status row.
     */
    void entry() override
    {
        m_locSelection = false;
        m_wmcTft.UpdateStatus("POWER OFF", false, WmcTft::color_red);
        m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
    }

    /**
     * Handle received data.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOn: transit<statePowerOn>(); break;
        case Z21Slave::programmingMode: transit<statePowerProgrammingMode>(); break;
        case Z21Slave::locinfo:
            updateLocInfoOnScreen(false);
            m_locLib.SpeedUpdate(m_WmcLocInfoReceived->Speed);

            if (m_WmcLocInfoReceived->Direction == Z21Slave::locDirectionForward)
            {
                m_locLib.DirectionSet(directionForward);
            }
            else
            {
                m_locLib.DirectionSet(directionBackWard);
            }
            break;
        case Z21Slave::locLibraryData:
            m_WmcLocLibInfo = m_z21Slave.LanXLocLibData();

            /* First database data show status... */
            if (m_WmcLocLibInfo->Actual == 0)
            {
                m_wmcTft.UpdateStatus("RECEIVING", false, WmcTft::color_white);
            }

            /* If loc not present store it. */
            if (m_locLib.CheckLoc(m_WmcLocLibInfo->Address) == 255)
            {
                m_locLib.StoreLoc(m_WmcLocLibInfo->Address, locFunctionAssignment, m_WmcLocLibInfo->NameStr,
                    LocLib::storeAddNoAutoSelect);
                m_wmcTft.UpdateSelectedAndNumberOfLocs(
                    m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
            }

            /* If all locs received sort... */
            if ((m_WmcLocLibInfo->Actual + 1) == m_WmcLocLibInfo->Total)
            {
                m_wmcTft.UpdateStatus("SORTING  ", false, WmcTft::color_white);
                m_locLib.LocBubbleSort();
                m_wmcTft.UpdateStatus("POWER OFF", false, WmcTft::color_red);
            }
            break;
        default: break;
        }
    }

    void react(updateEvent500msec const&)
    {
        m_z21Slave.LanXGetLocoInfo(m_locLib.GetActualLocAddress());
        WmcCheckForDataTx();
    };

    /**
     * Keep alive.
     */
    void react(updateEvent3sec const&) override
    {
        m_z21Slave.LanXGetLocoInfo(m_locLib.GetActualLocAddress());
        WmcCheckForDataTx();
    }

    /**
     * Check button event data.
     */
    void react(pushButtonsEvent const& e) override
    {
        switch (e.Button)
        {
        case button_power:
            /* Power on request. */
            m_z21Slave.LanSetTrackPowerOn();
            WmcCheckForDataTx();
            break;
        default: break;
        }
    }

    /**
     * Check pulse switch event data.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case pushturn:
            /* Select next or previous loc. */
            if (e.Delta != 0)
            {
                m_locLib.GetNextLoc(e.Delta);
                m_z21Slave.LanXGetLocoInfo(m_locLib.GetActualLocAddress());
                WmcCheckForDataTx();
                m_wmcTft.UpdateSelectedAndNumberOfLocs(
                    m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
                m_locSelection = true;
            }
            break;
        case pushedShort:
            /* Power on request. */
            m_z21Slave.LanSetTrackPowerOn();
            WmcCheckForDataTx();
            break;
        case pushedlong: transit<stateMainMenu1>(); break;
        default: break;
        }
    }
};

/***********************************************************************************************************************
 * Control is on, control the loc speed and functions, go back to power off or
 * select another locomotive.
 */
class statePowerOn : public wmcApp
{
    /**
     * Update status row.
     */
    void entry() override
    {
        m_locSelection              = false;
        m_WmcLocSpeedRequestPending = false;
        m_wmcTft.UpdateStatus("POWER ON", false, WmcTft::color_green);
        m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
    };

    /**
     * Handle received data.
     */
    void react(updateEvent5msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::emergencyStop: transit<stateEmergencyStop>(); break;
        case Z21Slave::trackPowerOff: transit<statePowerOff>(); break;
        case Z21Slave::programmingMode: transit<statePowerProgrammingMode>(); break;
        case Z21Slave::locinfo:
            updateLocInfoOnScreen(false);
            m_WmcLocSpeedRequestPending = false;
            m_locLib.SpeedUpdate(m_WmcLocInfoReceived->Speed);
            if (m_WmcLocInfoReceived->Direction == Z21Slave::locDirectionForward)
            {
                m_locLib.DirectionSet(directionForward);
            }
            else
            {
                m_locLib.DirectionSet(directionBackWard);
            }
            break;
        case Z21Slave::locLibraryData: break;
        default: break;
        }
    };

    void react(updateEvent50msec const&) override {}

    /**
     * Request loc info if for some reason no repsonse was received.
     */
    void react(updateEvent500msec const&)
    {
        m_z21Slave.LanXGetLocoInfo(m_locLib.GetActualLocAddress());
        WmcCheckForDataTx();
    };

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        uint16_t Speed = 0;

        switch (e.Status)
        {
        case pushturn:
            /* Select next or previous loc. */
            if (e.Delta != 0)
            {
                m_locLib.GetNextLoc(e.Delta);
                m_wmcTft.UpdateSelectedAndNumberOfLocs(
                    m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
                m_z21Slave.LanXGetLocoInfo(m_locLib.GetActualLocAddress());
                WmcCheckForDataTx();
                m_locSelection = true;
            }
            break;
        case turn:
            /* Increase or decrease speed. */
            if (m_WmcLocSpeedRequestPending == false)
            {
                Speed = m_locLib.SpeedSet(e.Delta);
                if (Speed != 0xFFFF)
                {
                    m_WmcLocSpeedRequestPending = true;
                    PrepareLanXSetLocoDriveAndTransmit(Speed);
                }
            }
            break;
        case pushedShort:
            /* Stop or change direction when speed is zero. */
            Speed = m_locLib.SpeedSet(0);
            if (Speed != 0xFFFF)
            {
                PrepareLanXSetLocoDriveAndTransmit(Speed);
            }
            break;
        case pushedNormal:
            /* Change direction. */
            m_locLib.DirectionToggle();
            PrepareLanXSetLocoDriveAndTransmit(m_locLib.SpeedGet());
            break;
        case pushedlong:
            m_CvPomProgramming            = true;
            m_CvPomProgrammingFromPowerOn = true;
            transit<stateCvProgramming>();
            break;
        }
    };

    /**
     * Handle button events.
     */
    void react(pushButtonsEvent const& e) override
    {
        uint8_t Function = 0;
        switch (e.Button)
        {
        case button_power:
            if (m_EmergencyStopEnabled == false)
            {
                m_z21Slave.LanSetTrackPowerOff();
            }
            else
            {
                m_z21Slave.LanSetStop();
            }
            WmcCheckForDataTx();
            break;
        case button_0:
            Function = m_locLib.FunctionAssignedGet(static_cast<uint8_t>(e.Button));
            m_locLib.FunctionToggle(Function);
            m_z21Slave.LanXSetLocoFunction(m_locLib.GetActualLocAddress(), Function, Z21Slave::toggle);
            WmcCheckForDataTx();
            break;
        case button_1:
        case button_2:
        case button_3:
        case button_4:
            Function = m_locLib.FunctionAssignedGet(static_cast<uint8_t>(e.Button));
            m_locLib.FunctionToggle(Function);
            if (m_locLib.FunctionStatusGet(Function) == LocLib::functionOn)
            {
                m_z21Slave.LanXSetLocoFunction(m_locLib.GetActualLocAddress(), Function, Z21Slave::on);
            }
            else
            {
                m_z21Slave.LanXSetLocoFunction(m_locLib.GetActualLocAddress(), Function, Z21Slave::off);
            }
            WmcCheckForDataTx();
            break;
        case button_5:
            m_wmcTft.Clear();
            transit<stateTurnoutControl>();
            break;
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Emergency stop, only react on power button and function buttons.
 */
class stateEmergencyStop : public wmcApp
{
    /**
     * Update status row.
     */
    void entry() override
    {
        m_locSelection              = false;
        m_WmcLocSpeedRequestPending = false;
        m_wmcTft.UpdateStatus("POWER ON", false, WmcTft::color_yellow);
        m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());

        /* Force speed to zero on screen. */
        m_locLib.SpeedUpdate(0);
        updateLocInfoOnScreen(false);
    };

    /**
     * Handle received data.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOff: transit<statePowerOff>(); break;
        case Z21Slave::trackPowerOn: transit<statePowerOn>(); break;
        case Z21Slave::emergencyStop: break;
        case Z21Slave::programmingMode: break;
        case Z21Slave::locinfo:
            updateLocInfoOnScreen(false);
            m_WmcLocSpeedRequestPending = false;
            m_locLib.SpeedUpdate(m_WmcLocInfoReceived->Speed);
            if (m_WmcLocInfoReceived->Direction == Z21Slave::locDirectionForward)
            {
                m_locLib.DirectionSet(directionForward);
            }
            else
            {
                m_locLib.DirectionSet(directionBackWard);
            }
            break;
        case Z21Slave::locLibraryData: break;
        default: break;
        }
    };

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case pushturn:
        case turn: break;
        case pushedNormal:
            /* Change direction. */
            m_locLib.DirectionToggle();
            PrepareLanXSetLocoDriveAndTransmit(m_locLib.SpeedGet());
            break;
        case pushedShort:
        case pushedlong: transit<stateMainMenu1>(); break;
        }
    };

    /**
     * Handle button events.
     */
    void react(pushButtonsEvent const& e) override
    {
        uint8_t Function = 0;
        switch (e.Button)
        {
        case button_power:
            m_z21Slave.LanSetTrackPowerOn();
            WmcCheckForDataTx();
            break;
        case button_0:
        case button_1:
        case button_2:
        case button_3:
        case button_4:
            Function = m_locLib.FunctionAssignedGet(static_cast<uint8_t>(e.Button));
            m_locLib.FunctionToggle(Function);
            if (m_locLib.FunctionStatusGet(Function) == LocLib::functionOn)
            {
                m_z21Slave.LanXSetLocoFunction(m_locLib.GetActualLocAddress(), Function, Z21Slave::on);
            }
            else
            {
                m_z21Slave.LanXSetLocoFunction(m_locLib.GetActualLocAddress(), Function, Z21Slave::off);
            }
            WmcCheckForDataTx();
            break;
        case button_5:
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Another Z21 device is in programming mode, do nothing....
 */
class statePowerProgrammingMode : public wmcApp
{
    /**
     * Update status row.
     */
    void entry() override
    {
        m_locSelection = false;
        m_wmcTft.UpdateStatus("PROG MODE", false, WmcTft::color_yellow);
        m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
    };

    /**
     * Handle received data.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOff: transit<statePowerOff>(); break;
        case Z21Slave::trackPowerOn: transit<statePowerOn>(); break;
        default: break;
        }
    };

    /**
     * Handle button events.
     */
    void react(pushButtonsEvent const& e) override
    {
        switch (e.Button)
        {
        case button_power:
            m_z21Slave.LanSetTrackPowerOff();
            WmcCheckForDataTx();
            break;
        case button_0:
        case button_1:
        case button_2:
        case button_3:
        case button_4:
        case button_5:
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Turnout control.
 */
class stateTurnoutControl : public wmcApp
{
    /**
     * Show turnout screen.
     */
    void entry() override
    {
        m_TurnOutDirection = Z21Slave::directionOff;

        m_wmcTft.UpdateStatus("TURNOUT", true, WmcTft::color_green);
        m_wmcTft.ShowTurnoutScreen();
        m_wmcTft.ShowTurnoutAddress(m_TurnOutAddress);
        m_wmcTft.ShowTurnoutDirection(static_cast<uint8_t>(m_TurnOutDirection));
    };

    /**
     * Handle received data.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOff: transit<stateTurnoutControlPowerOff>(); break;
        default: break;
        }

        /* When turnout active sent after 500msec off command. */
        if (m_TurnOutDirection != Z21Slave::directionOff)
        {
            if ((millis() - m_TurnoutOffDelay) > 500)
            {
                m_TurnOutDirection = Z21Slave::directionOff;
                m_z21Slave.LanXSetTurnout(m_TurnOutAddress - 1, m_TurnOutDirection);
                WmcCheckForDataTx();
                m_wmcTft.ShowTurnoutDirection(static_cast<uint8_t>(m_TurnOutDirection));
            }
        }
    };

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        bool updateScreen = false;
        switch (e.Status)
        {
        case pushturn: break;
        case turn:
            if (e.Delta > 0)
            {
                /* Increase address and check for overrrun. */
                if (m_TurnOutAddress < ADDRESS_TURNOUT_MAX)
                {
                    m_TurnOutAddress++;
                }
                else
                {
                    m_TurnOutAddress = ADDRESS_TURNOUT_MIN;
                }

                updateScreen = true;
            }
            else if (e.Delta < 0)
            {
                /* Decrease address and handle address 0. */
                if (m_TurnOutAddress > ADDRESS_TURNOUT_MIN)
                {
                    m_TurnOutAddress--;
                }
                else
                {
                    m_TurnOutAddress = ADDRESS_TURNOUT_MAX;
                }
                updateScreen = true;
            }
            break;
        case pushedShort:
            /* Reset turnout address. */
            m_TurnOutAddress = ADDRESS_TURNOUT_MIN;
            updateScreen     = true;
            break;
        case pushedNormal:
        case pushedlong:
            /* Back to loc control. */
            transit<stateInitStatusGet>();
            break;
        default: break;
        }

        /* Update address on display if required. */
        if (updateScreen == true)
        {
            m_wmcTft.ShowTurnoutAddress(m_TurnOutAddress);
        }
    };

    /**
     * Handle button events.
     */
    void react(pushButtonsEvent const& e) override
    {
        bool updateScreen       = true;
        bool sentTurnOutCommand = false;

        /* Handle button requests. */
        switch (e.Button)
        {
        case button_power:
            m_z21Slave.LanSetTrackPowerOff();
            WmcCheckForDataTx();
            break;
        case button_0: m_TurnOutAddress++; break;
        case button_1: m_TurnOutAddress += 10; break;
        case button_2: m_TurnOutAddress += 100; break;
        case button_3: m_TurnOutAddress += 1000; break;
        case button_4:
            m_TurnOutDirection = Z21Slave::directionForward;
            m_TurnoutOffDelay  = millis();
            updateScreen       = false;
            sentTurnOutCommand = true;
            break;
        case button_5:
            m_TurnOutDirection = Z21Slave::directionTurn;
            m_TurnoutOffDelay  = millis();
            updateScreen       = false;
            sentTurnOutCommand = true;
            break;
        default: break;
        }

        if (updateScreen == true)
        {
            if (m_TurnOutAddress > ADDRESS_TURNOUT_MAX)
            {
                m_TurnOutAddress = 1;
            }
            m_wmcTft.ShowTurnoutAddress(m_TurnOutAddress);
        }

        if (sentTurnOutCommand == true)
        {
            /* Sent command and show turnout direction. */
            m_z21Slave.LanXSetTurnout(m_TurnOutAddress - 1, m_TurnOutDirection);
            WmcCheckForDataTx();
            m_wmcTft.ShowTurnoutDirection(static_cast<uint8_t>(m_TurnOutDirection));
        }
    };

    /**
     * When exit and turnout active transmit off command.
     */
    void exit() override
    {
        if (m_TurnOutDirection != Z21Slave::directionOff)
        {
            m_TurnOutDirection = Z21Slave::directionOff;
            m_z21Slave.LanXSetTurnout(m_TurnOutAddress - 1, m_TurnOutDirection);
            WmcCheckForDataTx();
            m_wmcTft.ShowTurnoutDirection(static_cast<uint8_t>(m_TurnOutDirection));
        }
    }
};

/***********************************************************************************************************************
 * Power off screen and handling for turnout control.
 */
class stateTurnoutControlPowerOff : public wmcApp
{
    /**
     * Show turnout screen.
     */
    void entry() override
    {
        m_wmcTft.UpdateStatus("TURNOUT", true, WmcTft::color_red);
        m_TrackPower = powerState::off;
    };

    /**
     * Handle received data.
     */
    void react(updateEvent50msec const&) override
    {
        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOff: break;
        case Z21Slave::trackPowerOn:
            m_TrackPower = powerState::on;
            transit<stateTurnoutControl>();
            break;
        default: break;
        }
    };

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case pushturn: break;
        case turn:
        case pushedShort: break;
        case pushedNormal:
            /* Back to loc control. */
            transit<stateInitLocInfoGet>();
            break;
        default: break;
        }
    };

    /**
     * Handle button events.
     */
    void react(pushButtonsEvent const& e) override
    {
        /* Handle button requests. */
        switch (e.Button)
        {
        case button_power:
            m_z21Slave.LanSetTrackPowerOn();
            WmcCheckForDataTx();
            break;
        case button_0:
        case button_1:
        case button_2:
        case button_3:
        case button_4:
        case button_5: break;
        default: break;
        }
    };
};

/***********************************************************************************************************************
 * Show main menu and handle the request.
 */
class stateMainMenu1 : public wmcApp
{
    /**
     * Show menu on screen.
     */
    void entry() override { m_wmcTft.ShowMenu1(); };

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case turn: transit<stateMainMenu2>(); break;
        case pushturn: break;
        case pushedShort:
        case pushedNormal:
        case pushedlong:
            m_locSelection = true;
            transit<stateInitStatusGet>();
            break;
        }
    }

    /**
     * Handle switch events.
     */
    void react(pushButtonsEvent const& e) override
    {
        /* Handle menu request. */
        switch (e.Button)
        {
        case button_1:
            m_locAddressAdd = m_locLib.GetActualLocAddress();
            transit<stateMenuLocAdd>();
            break;
        case button_2: transit<stateMenuLocFunctionsChange>(); break;
        case button_3: transit<stateMenuLocDelete>(); break;
        case button_4:
            m_CvPomProgramming = false;
            transit<stateCvProgramming>();
            break;
        case button_5:
            m_CvPomProgramming = true;
            transit<stateCvProgramming>();
            break;
        case button_power:
            m_locSelection = true;
            transit<stateInitStatusGet>();
            break;
        case button_0:
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Show main menu 2 and handle the request.
 */
class stateMainMenu2 : public wmcApp
{
    /**
     * Show menu on screen.
     */
    void entry() override { m_wmcTft.ShowMenu2(m_LocStorage.EmergencyOptionGet(), true); };

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case turn: transit<stateMainMenu1>(); break;
        case pushturn: break;
        case pushedShort:
        case pushedNormal:
        case pushedlong:
            m_locSelection = true;
            transit<stateInitStatusGet>();
            break;
        }
    }

    /**
     * Handle switch events.
     */
    void react(pushButtonsEvent const& e) override
    {
        /* Handle menu request. */
        switch (e.Button)
        {
        case button_0:
        case button_1: break;
        case button_2:
            /* Toggle emergency stop or power off for power button. */
            if (m_LocStorage.EmergencyOptionGet() == false)
            {
                m_LocStorage.EmergencyOptionSet(1);
                m_EmergencyStopEnabled = true;
                m_wmcTft.ShowMenu2(true, false);
            }
            else
            {
                m_LocStorage.EmergencyOptionSet(0);
                m_EmergencyStopEnabled = false;
                m_wmcTft.ShowMenu2(false, false);
            }
            break;
        case button_3: transit<stateMenuTransmitLocDatabase>(); break;
        case button_4:
            /* Erase all locomotives and ask user to perform reset. */
            m_WifiUdp.stop();
            m_wmcTft.ShowErase();
            m_locLib.InitialLocStore();
            m_LocStorage.NumberOfLocsSet(1);
            m_wmcTft.Clear();
            m_wmcTft.CommandLine();
            while (1)
            {
            };
            break;
        case button_5:
            /* Erase all locs and settings and ask user to perform reset. */
            m_WifiUdp.stop();
            m_wmcTft.ShowErase();
            m_locLib.InitialLocStore();
            m_LocStorage.AcOptionSet(0);
            m_LocStorage.InvalidateAdc();
            m_LocStorage.NumberOfLocsSet(1);
            m_LocStorage.EmergencyOptionSet(0);
            m_WmcCommandLine.IpSettingsDefault();
            m_wmcTft.Clear();
            m_wmcTft.CommandLine();
            while (1)
            {
            };
            break;
        case button_power:
            m_locSelection = true;
            transit<stateInitStatusGet>();
            break;
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Add a loc.
 */
class stateMenuLocAdd : public wmcApp
{
    /**
     * Show loc menu add screen.
     */
    void entry() override
    {
        // Show loc add screen.
        m_wmcTft.Clear();
        m_wmcTft.UpdateStatus("ADD LOC", true, WmcTft::color_green);
        m_wmcTft.ShowLocSymbolFw(WmcTft::color_white);
        m_wmcTft.ShowlocAddress(m_locAddressAdd, WmcTft::color_green);
        m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
    };

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case turn:
            /* Increase or decrease loc address to be added. */
            if (e.Delta > 0)
            {
                m_locAddressAdd++;
                m_locAddressAdd = m_locLib.limitLocAddress(m_locAddressAdd);
                m_wmcTft.ShowlocAddress(m_locAddressAdd, WmcTft::color_green);
            }
            else if (e.Delta < 0)
            {
                m_locAddressAdd--;
                m_locAddressAdd = m_locLib.limitLocAddress(m_locAddressAdd);
                m_wmcTft.ShowlocAddress(m_locAddressAdd, WmcTft::color_green);
            }
            break;
        case pushturn: break;
        case pushedNormal:
        case pushedlong:
            /* If loc is not present goto add functions else red address indicating
             * loc already present. */
            if (m_locLib.CheckLoc(m_locAddressAdd) != 255)
            {
                m_wmcTft.ShowlocAddress(m_locAddressAdd, WmcTft::color_red);
            }
            else
            {
                transit<stateMenuLocFunctionsAdd>();
            }
            break;
        default: break;
        }
    };

    /**
     * Handle button events for easier / faster increase of address and reset of
     * address.
     */
    void react(pushButtonsEvent const& e) override
    {
        bool updateScreen = true;

        switch (e.Button)
        {
        case button_0: m_locAddressAdd++; break;
        case button_1: m_locAddressAdd += 10; break;
        case button_2: m_locAddressAdd += 100; break;
        case button_3: m_locAddressAdd += 1000; break;
        case button_4: m_locAddressAdd = 1; break;
        case button_5:
            /* If loc is not present goto add functions else red address indicating
             * loc already present. */
            if (m_locLib.CheckLoc(m_locAddressAdd) != 255)
            {
                updateScreen = false;
                m_wmcTft.ShowlocAddress(m_locAddressAdd, WmcTft::color_red);
            }
            else
            {
                transit<stateMenuLocFunctionsAdd>();
            }
            break;
        case button_power:
            updateScreen = false;
            transit<stateMainMenu1>();
            break;
        case button_none: updateScreen = false; break;
        }

        if (updateScreen == true)
        {
            m_locAddressAdd = m_locLib.limitLocAddress(m_locAddressAdd);
            m_wmcTft.ShowlocAddress(m_locAddressAdd, WmcTft::color_green);
        }
    };
};

/***********************************************************************************************************************
 * Set the functions of the loc to be added.
 */
class stateMenuLocFunctionsAdd : public wmcApp
{
    /**
     * Show function add screen.
     */
    void entry() override
    {
        uint8_t Index;

        m_wmcTft.UpdateStatus("FUNCTIONS", true, WmcTft::color_green);
        m_locFunctionAdd = 0;
        for (Index = 0; Index < 5; Index++)
        {
            m_locFunctionAssignment[Index] = Index;
        }

        m_wmcTft.FunctionAddSet();
        m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
    };

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case turn:
            /* ncrease of decrease the function. */
            if (e.Delta > 0)
            {
                m_locFunctionAdd++;
                if (m_locFunctionAdd > FUNCTION_MAX)
                {
                    m_locFunctionAdd = FUNCTION_MIN;
                }
                m_wmcTft.FunctionAddUpdate(m_locFunctionAdd);
            }
            else if (e.Delta < 0)
            {
                if (m_locFunctionAdd == FUNCTION_MIN)
                {
                    m_locFunctionAdd = FUNCTION_MAX;
                }
                else
                {
                    m_locFunctionAdd--;
                }
                m_wmcTft.FunctionAddUpdate(m_locFunctionAdd);
            }
            break;
        case pushedNormal:
            /* Store loc functions */
            m_locLib.StoreLoc(m_locAddressAdd, m_locFunctionAssignment, NULL, LocLib::storeAdd);
            m_locLib.LocBubbleSort();
            m_locAddressAdd++;
            transit<stateMenuLocAdd>();
            break;
        default: break;
        }
    };

    /**
     * Handle button events.
     */
    void react(pushButtonsEvent const& e) override
    {
        switch (e.Button)
        {
        case button_0:
            /* Button 0 only for light or other functions. */
            m_locFunctionAssignment[static_cast<uint8_t>(e.Button)] = m_locFunctionAdd;
            m_wmcTft.UpdateFunction(
                static_cast<uint8_t>(e.Button), m_locFunctionAssignment[static_cast<uint8_t>(e.Button)]);
            break;
        case button_1:
        case button_2:
        case button_3:
        case button_4:
            /* Rest of buttons for oher functions except light. */
            if (m_locFunctionAdd != 0)
            {
                m_locFunctionAssignment[static_cast<uint8_t>(e.Button)] = m_locFunctionAdd;
                m_wmcTft.UpdateFunction(
                    static_cast<uint8_t>(e.Button), m_locFunctionAssignment[static_cast<uint8_t>(e.Button)]);
            }
            break;
        case button_power: transit<stateMainMenu1>(); break;
        case button_5:
            /* Store loc functions */
            m_locLib.StoreLoc(m_locAddressAdd, m_locFunctionAssignment, NULL, LocLib::storeAdd);
            m_locLib.LocBubbleSort();
            m_locAddressAdd++;
            transit<stateMenuLocAdd>();
            break;
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Changer functions of a loc already present.
 */
class stateMenuLocFunctionsChange : public wmcApp
{
    /**
     * Show change function screen.
     */
    void entry() override
    {
        uint8_t Index;

        m_wmcTft.Clear();
        m_locFunctionChange = 0;
        m_locAddressChange  = m_locLib.GetActualLocAddress();
        m_wmcTft.UpdateStatus("CHANGE FUNC", true, WmcTft::color_green);
        m_wmcTft.ShowLocSymbolFw(WmcTft::color_white);
        m_wmcTft.ShowlocAddress(m_locAddressChange, WmcTft::color_green);
        m_wmcTft.FunctionAddUpdate(m_locFunctionChange);
        m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());

        for (Index = 0; Index < 5; Index++)
        {
            m_locFunctionAssignment[Index] = m_locLib.FunctionAssignedGet(Index);
            m_wmcTft.UpdateFunction(Index, m_locFunctionAssignment[Index]);
        }
    }

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        uint8_t Index = 0;

        switch (e.Status)
        {
        case turn:
            /* Change function. */
            if (e.Delta > 0)
            {
                m_locFunctionChange++;
                if (m_locFunctionChange > FUNCTION_MAX)
                {
                    m_locFunctionChange = FUNCTION_MIN;
                }
                m_wmcTft.FunctionAddUpdate(m_locFunctionChange);
            }
            else if (e.Delta < 0)
            {
                if (m_locFunctionChange == FUNCTION_MIN)
                {
                    m_locFunctionChange = FUNCTION_MAX;
                }
                else
                {
                    m_locFunctionChange--;
                }
                m_wmcTft.FunctionAddUpdate(m_locFunctionChange);
            }
            break;
        case pushturn:
            /* Select another loc and update function data of newly selected loc. */
            m_locAddressChange = m_locLib.GetNextLoc(e.Delta);
            m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());

            for (Index = 0; Index < 5; Index++)
            {
                m_locFunctionAssignment[Index] = m_locLib.FunctionAssignedGet(Index);
                m_wmcTft.UpdateFunction(Index, m_locFunctionAssignment[Index]);
            }

            m_wmcTft.ShowlocAddress(m_locAddressChange, WmcTft::color_green);
            break;
        case pushedNormal:
        case pushedlong:
            /* Store changed data and yellow text indicating data is stored. */
            m_locLib.StoreLoc(m_locAddressChange, m_locFunctionAssignment, NULL, LocLib::storeChange);
            m_wmcTft.ShowlocAddress(m_locAddressChange, WmcTft::color_yellow);
            break;
        default: break;
        }
    };

    /**
     * Handle button events.
     */
    void react(pushButtonsEvent const& e) override
    {
        switch (e.Button)
        {
        case button_0:
            /* Button 0 only for light or other functions. */
            m_locFunctionAssignment[static_cast<uint8_t>(e.Button)] = m_locFunctionChange;
            m_wmcTft.UpdateFunction(
                static_cast<uint8_t>(e.Button), m_locFunctionAssignment[static_cast<uint8_t>(e.Button)]);
            break;
        case button_1:
        case button_2:
        case button_3:
        case button_4:
            /* Rest of buttons for other functions except light. */
            if (m_locFunctionChange != 0)
            {
                m_locFunctionAssignment[static_cast<uint8_t>(e.Button)] = m_locFunctionChange;
                m_wmcTft.UpdateFunction(
                    static_cast<uint8_t>(e.Button), m_locFunctionAssignment[static_cast<uint8_t>(e.Button)]);
            }
            break;
        case button_power: transit<stateMainMenu1>(); break;
        case button_5:
            /* Store changed data and yellow text indicating data is stored. */
            m_locLib.StoreLoc(m_locAddressChange, m_locFunctionAssignment, NULL, LocLib::storeChange);
            m_wmcTft.ShowlocAddress(m_locAddressChange, WmcTft::color_yellow);
            break;
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Delete a loc.
 */
class stateMenuLocDelete : public wmcApp
{
    /**
     * Show delete screen.
     */
    void entry() override
    {
        m_wmcTft.Clear();
        m_locAddressDelete = m_locLib.GetActualLocAddress();
        m_wmcTft.UpdateStatus("DELETE", true, WmcTft::color_green);
        m_wmcTft.ShowLocSymbolFw(WmcTft::color_white);
        m_wmcTft.ShowlocAddress(m_locAddressDelete, WmcTft::color_green);
        m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
        m_LocAddresActualDelete = m_locLib.GetActualLocAddress();
    }

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case turn:
            /* Select loc to be deleted. */
            m_locAddressDelete = m_locLib.GetNextLoc(e.Delta);
            m_wmcTft.UpdateSelectedAndNumberOfLocs(m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
            m_wmcTft.ShowlocAddress(m_locAddressDelete, WmcTft::color_green);
            break;
        case pushedNormal:
        case pushedlong:
            /* Remove loc. */
            if (m_locLib.GetNumberOfLocs() > 1)
            {
                m_wmcTft.UpdateStatus("DELETING", true, WmcTft::color_red);
                m_locLib.RemoveLoc(m_locAddressDelete);
                m_wmcTft.UpdateSelectedAndNumberOfLocs(
                    m_locLib.GetActualSelectedLocIndex(), m_locLib.GetNumberOfLocs());
                m_locAddressDelete = m_locLib.GetActualLocAddress();
                m_wmcTft.ShowlocAddress(m_locAddressDelete, WmcTft::color_green);
                m_wmcTft.UpdateStatus("DELETE", true, WmcTft::color_green);
            }

            /* In case the selected loc was the same as the active controlled loc
               update last selected loc entry.*/
            if (m_locAddressDelete == m_locAddressDelete)
            {
                m_locAddressDelete = m_locLib.GetActualLocAddress();
                m_LocStorage.SelectedLocIndexStore(m_locLib.GetActualSelectedLocIndex() - 1);
            }
            else
            {
                m_locAddressDelete = m_locLib.GetActualLocAddress();
            }
            break;
        default: break;
        }
    }

    /**
     * Handle button events, just go back to main menu on each button.
     */
    void react(pushButtonsEvent const& e) override
    {
        switch (e.Button)
        {
        case button_0:
        case button_1:
        case button_2:
        case button_3:
        case button_4:
        case button_5:
        case button_power: transit<stateMainMenu1>(); break;
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Transmit loc data on XpressNet
 */
class stateMenuTransmitLocDatabase : public wmcApp
{
    void entry() override
    {
        m_locDbDataTransmitCnt       = 0;
        m_locDbDataTransmitCntRepeat = 0;
        m_wmcTft.UpdateStatus("SEND LOC DATA", true, WmcTft::color_white);

        /* Update status row. */
        m_wmcTft.UpdateTransmitCount(
            static_cast<uint8_t>(m_locDbDataTransmitCnt + 1), static_cast<uint8_t>(m_locLib.GetNumberOfLocs()));
    }

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        switch (e.Status)
        {
        case turn:
        case pushedNormal:
        case pushedlong:
            transit<stateMainMenu2>();
            break;
            break;
        default: break;
        }
    }

    /**
     * Transmit loc data.
     */
    void react(updateEvent100msec const&)
    {
        LocLibData* LocDbData;

        /* The loc database needs to be transmitted twice.. */
        if (m_locDbDataTransmitCntRepeat > 2)
        {
            m_locDbDataTransmitCntRepeat = 1;
            m_locDbDataTransmitCnt++;

            /* Update status row. */
            m_wmcTft.UpdateTransmitCount(
                static_cast<uint8_t>(m_locDbDataTransmitCnt + 1), static_cast<uint8_t>(m_locLib.GetNumberOfLocs()));
        }
        m_locDbDataTransmitCntRepeat++;

        // If last loc transmitted back to menu else transmit loc data,
        if (m_locDbDataTransmitCnt >= m_locLib.GetNumberOfLocs())
        {
            transit<stateMainMenu2>();
        }
        else
        {
            LocDbData = m_locLib.LocGetAllDataByIndex(m_locDbDataTransmitCnt);
            m_z21Slave.LanXLocLibDataTransmit(LocDbData->Addres, m_locDbDataTransmitCnt,
                static_cast<uint8_t>(m_locLib.GetNumberOfLocs()), LocDbData->Name);
            WmcCheckForDataTx();
        }
    }

    /**
     * Handle button events, just go back to main menu on each button.
     */
    void react(pushButtonsEvent const& e) override
    {
        switch (e.Button)
        {
        case button_0:
        case button_1:
        case button_2:
        case button_3:
        case button_4:
        case button_5:
        case button_power: transit<stateMainMenu2>(); break;
        case button_none: break;
        }
    };
};

/***********************************************************************************************************************
 * Command lie interface active state.
 */
class stateCommandLineInterfaceActive : public wmcApp
{
    /**
     * Show delete screen.
     */
    void entry() override
    {
        m_WifiUdp.stop();
        m_wmcTft.Clear();
        m_wmcTft.UpdateStatus("COMMAND LINE", true, WmcTft::color_green);
        m_wmcTft.CommandLine();
    };
};

/***********************************************************************************************************************
 * CV programming main state.
 */
class stateCvProgramming : public wmcApp
{
    /**
     * Show delete screen.
     */
    void entry() override
    {
        cvEvent EventCv;
        m_wmcTft.Clear();
        if (m_CvPomProgramming == false)
        {
            EventCv.EventData = startCv;
            m_wmcTft.UpdateStatus("CV PROGRAMMING", true, WmcTft::color_green);
        }
        else
        {
            EventCv.EventData = startPom;
            m_wmcTft.UpdateStatus("POM PROGRAMMING", true, WmcTft::color_green);
            m_z21Slave.LanSetTrackPowerOn();
            WmcCheckForDataTx();
        }

        send_event(EventCv);
    };

    /**
     * Handle received Z21 data.
     */
    void react(updateEvent50msec const&) override
    {
        cvEvent EventCv;
        cvpushButtonEvent ButtonEvent;
        Z21Slave::cvData* cvDataPtr = NULL;

        switch (WmcCheckForDataRx())
        {
        case Z21Slave::trackPowerOff:
            m_TrackPower = powerState::off;
            /* Use power off button event to stop CV programming. */
            ButtonEvent.EventData.Button = button_power;
            send_event(ButtonEvent);
            transit<stateInitLocInfoGet>();
            break;
        case Z21Slave::trackPowerOn:
            if ((m_CvPomProgramming == false) || (m_CvPomProgrammingFromPowerOn == true))
            {
                ButtonEvent.EventData.Button = button_power;
                send_event(ButtonEvent);
                transit<stateInitLocInfoGet>();
            }
            break;
        case Z21Slave::programmingCvNackSc:
            EventCv.EventData = cvNack;
            send_event(EventCv);
            break;
        case Z21Slave::programmingCvResult:
            cvDataPtr = m_z21Slave.LanXCvResult();

            EventCv.EventData = cvData;
            EventCv.cvNumber  = cvDataPtr->Number;
            EventCv.cvValue   = cvDataPtr->Value;
            send_event(EventCv);
            break;
        default: break;
        }
    };

    /**
     * Keep alive by requesting loc status. Requesting power system status forces
     * the CV mode back to normal mode...
     */
    void react(updateEvent3sec const&) override
    {
        m_z21Slave.LanXGetLocoInfo(m_locLib.GetActualLocAddress());
        WmcCheckForDataTx();
    }

    /**
     * Handle pulse switch events.
     */
    void react(pulseSwitchEvent const& e) override
    {
        cvpulseSwitchEvent Event;

        switch (e.Status)
        {
        case pushturn:
        case turn:
        case pushedShort:
        case pushedNormal:
            /* Forward event */
            Event.EventData.Delta  = e.Delta;
            Event.EventData.Status = e.Status;
            send_event(Event);
            break;
        default: break;
        }
    };

    void react(updateEvent500msec const&) override
    {
        cvEvent EventCv;
        EventCv.EventData = update;
        send_event(EventCv);
    }

    /**
     * Handle button events.
     */
    void react(pushButtonsEvent const& e) override
    {
        cvpushButtonEvent Event;

        /* Handle menu request. */
        switch (e.Button)
        {
        case button_0:
        case button_1:
        case button_2:
        case button_3:
        case button_4:
        case button_5:
        case button_power:
            if (m_CvPomProgrammingFromPowerOn == false)
            {
                Event.EventData.Button = e.Button;
                send_event(Event);

                m_z21Slave.LanGetStatus();
                WmcCheckForDataTx();
                transit<stateMainMenu1>();
            }
            else
            {
                transit<stateInitStatusGet>();
            }
            break;
        case button_none: break;
        }
    };

    /**
     * Handle events from the cv state machine.
     */
    void react(cvProgEvent const& e) override
    {
        switch (e.Request)
        {
        case cvRead:
            m_z21Slave.LanCvRead(e.CvNumber);
            WmcCheckForDataTx();
            break;
        case cvWrite:
            m_z21Slave.LanCvWrite(e.CvNumber, e.CvValue);
            WmcCheckForDataTx();
            break;
        case pomWrite:
            m_z21Slave.LanXCvPomWriteByte(e.Address, e.CvNumber, e.CvValue);
            WmcCheckForDataTx();
            break;
        case cvStatusRequest: break;
        case cvExit:
            if (m_CvPomProgrammingFromPowerOn == false)
            {
                m_z21Slave.LanGetStatus();
                WmcCheckForDataTx();
                transit<stateMainMenu1>();
            }
            else
            {
                transit<stateInitStatusGet>();
            }
            break;
        }
    }

    /**
     * Exit handler.
     */
    void exit() override { m_CvPomProgrammingFromPowerOn = false; };
};

/***********************************************************************************************************************
 * Default event handlers when not declared in states itself.
 */
void wmcApp::react(pulseSwitchEvent const&){};
void wmcApp::react(pushButtonsEvent const&){};
void wmcApp::react(updateEvent5msec const&){};
void wmcApp::react(updateEvent50msec const&) { WmcCheckForDataRx(); };
void wmcApp::react(updateEvent100msec const&)
{
    bool Found    = false;
    uint8_t Index = 0;
    uint16_t readingIn;

    m_WmcCommandLine.Update();

    readingIn = analogRead(WMC_APP_ANALOG_IN);

    /* Button pressed ? */
    if (readingIn < m_AdcButtonValue[ADC_VALUES_ARRAY_REFERENCE_INDEX])
    {
        /* Check which button is pressed. */
        while ((Index < 6) && (Found == false))
        {
            /* One button results in ADC values ~10, check. */
            if (m_AdcButtonValue[Index] < 20)
            {
                if (readingIn <= m_AdcButtonValue[Index])
                {
                    /* Button value within defined borders... */
                    Found                 = true;
                    m_ButtonIndexPrevious = Index;
                }
                else
                {
                    Index++;
                }
            }
            else if ((readingIn > (m_AdcButtonValue[Index] - 20)) && (readingIn < (m_AdcButtonValue[Index] + 20)))
            {
                /* Button value within defined borders... */
                Found                 = true;
                m_ButtonIndexPrevious = Index;
            }
            else
            {
                /* Increase index for next check... */
                Index++;
            }
        }
    }

    if ((Found == false) && (m_ButtonPrevious == true))
    {
        // Valid button detected, throw event once after release of button..
        m_wmcPushButtonEvent.Button = static_cast<pushButtons>(m_ButtonIndexPrevious);
        send_event(m_wmcPushButtonEvent);
    }

    m_ButtonPrevious = Found;
};

void wmcApp::react(updateEvent500msec const&){};
void wmcApp::react(updateEvent3sec const&)
{
    m_z21Slave.LanGetStatus();
    WmcCheckForDataTx();
};
void wmcApp::react(cliEnterEvent const&) { transit<stateCommandLineInterfaceActive>(); };
void wmcApp::react(cvProgEvent const&){};

/***********************************************************************************************************************
 * Initial state.
 */
FSM_INITIAL_STATE(wmcApp, stateInit)

/***********************************************************************************************************************
 * Check for received Z21 data and process it.
 */
Z21Slave::dataType wmcApp::WmcCheckForDataRx(void)
{
    int WmcPacketBufferLength     = 0;
    Z21Slave::dataType returnData = Z21Slave::none;
#if WMC_APP_DEBUG_TX_RX == 1
    uint8_t Index;
#endif

    if (m_WifiUdp.parsePacket())
    {
        // We've received a packet, read the data from it into the buffer
        WmcPacketBufferLength = m_WifiUdp.read(m_WmcPacketBuffer, 40);

        if (WmcPacketBufferLength != 0)
        {
#if WMC_APP_DEBUG_TX_RX == 1
            Serial.print("RX : ");

            for (Index = 0; Index < WmcPacketBufferLength; Index++)
            {
                Serial.print(m_WmcPacketBuffer[Index], HEX);
                Serial.print(" ");
            }

            Serial.println("");
#endif
            // Process the data.
            returnData = m_z21Slave.ProcesDataRx(m_WmcPacketBuffer, sizeof(m_WmcPacketBuffer));
        }
    }

    return (returnData);
}

/***********************************************************************************************************************
 * Check for data to be transmitted.
 */
void wmcApp::WmcCheckForDataTx(void)
{
    uint8_t* DataTransmitPtr;
    IPAddress WmcUdpIp(m_IpAddresZ21[0], m_IpAddresZ21[1], m_IpAddresZ21[2], m_IpAddresZ21[3]);

#if WMC_APP_DEBUG_TX_RX == 1
    uint8_t Index;
#endif

    if (m_z21Slave.txDataPresent() == true)
    {
        DataTransmitPtr = m_z21Slave.GetDataTx();

#if WMC_APP_DEBUG_TX_RX == 1
        Serial.print("TX : ");

        for (Index = 0; Index < DataTransmitPtr[0]; Index++)
        {
            Serial.print(DataTransmitPtr[Index], HEX);
            Serial.print(" ");
        }

        Serial.println("");
#endif

        m_WifiUdp.beginPacket(WmcUdpIp, m_UdpLocalPort);
        m_WifiUdp.write(DataTransmitPtr, DataTransmitPtr[0]);
        m_WifiUdp.endPacket();
    }
}

/***********************************************************************************************************************
 * Convert loc data to tft loc data.
 */
void wmcApp::convertLocDataToDisplayData(Z21Slave::locInfo* Z21DataPtr, WmcTft::locoInfo* TftDataPtr)
{
    TftDataPtr->Address = Z21DataPtr->Address;
    TftDataPtr->Speed   = Z21DataPtr->Speed;

    switch (Z21DataPtr->Steps)
    {
    case Z21Slave::locDecoderSpeedSteps14: TftDataPtr->Steps = WmcTft::locoDecoderSpeedSteps14; break;
    case Z21Slave::locDecoderSpeedSteps28: TftDataPtr->Steps = WmcTft::locoDecoderSpeedSteps28; break;
    case Z21Slave::locDecoderSpeedSteps128: TftDataPtr->Steps = WmcTft::locoDecoderSpeedSteps128; break;
    case Z21Slave::locDecoderSpeedStepsUnknown: TftDataPtr->Steps = WmcTft::locoDecoderSpeedStepsUnknown; break;
    }

    switch (Z21DataPtr->Direction)
    {
    case Z21Slave::locDirectionForward: TftDataPtr->Direction = WmcTft::locoDirectionForward; break;
    case Z21Slave::locDirectionBackward: TftDataPtr->Direction = WmcTft::locoDirectionBackward; break;
    }

    switch (Z21DataPtr->Light)
    {
    case Z21Slave::locLightOn: TftDataPtr->Light = WmcTft::locoLightOn; break;
    case Z21Slave::locLightOff: TftDataPtr->Light = WmcTft::locoLightOff; break;
    }

    TftDataPtr->Functions = Z21DataPtr->Functions;
    TftDataPtr->Occupied  = Z21DataPtr->Occupied;
}

/***********************************************************************************************************************
 * Update loc info on screen.
 */
bool wmcApp::updateLocInfoOnScreen(bool updateAll)
{
    uint8_t Index        = 0;
    bool Result          = true;
    m_WmcLocInfoReceived = m_z21Slave.LanXLocoInfo();
    WmcTft::locoInfo locInfoActual;
    WmcTft::locoInfo locInfoPrevious;

    if (m_locLib.GetActualLocAddress() == m_WmcLocInfoReceived->Address)
    {
        switch (m_WmcLocInfoReceived->Steps)
        {
        case Z21Slave::locDecoderSpeedSteps14: m_locLib.DecoderStepsUpdate(decoderStep14); break;
        case Z21Slave::locDecoderSpeedSteps28: m_locLib.DecoderStepsUpdate(decoderStep28); break;
        case Z21Slave::locDecoderSpeedSteps128: m_locLib.DecoderStepsUpdate(decoderStep128); break;
        case Z21Slave::locDecoderSpeedStepsUnknown: m_locLib.DecoderStepsUpdate(decoderStep28); break;
        }

        for (Index = 0; Index < 5; Index++)
        {
            m_locFunctionAssignment[Index] = m_locLib.FunctionAssignedGet(Index);
        }

        /* Invert functions so function symbols are updated if new loc is selected
         * and set new direction. */
        if (m_locSelection == true)
        {
            m_WmcLocInfoControl.Functions = ~m_WmcLocInfoReceived->Functions;
            m_locSelection                = false;
        }

        convertLocDataToDisplayData(m_WmcLocInfoReceived, &locInfoActual);
        convertLocDataToDisplayData(&m_WmcLocInfoControl, &locInfoPrevious);
        m_wmcTft.UpdateLocInfo(
            &locInfoActual, &locInfoPrevious, m_locFunctionAssignment, m_locLib.GetLocName(), updateAll);

        memcpy(&m_WmcLocInfoControl, m_WmcLocInfoReceived, sizeof(Z21Slave::locInfo));
    }
    else
    {
        Result = false;
    }

    return (Result);
}

/***********************************************************************************************************************
 * Compose locomotive message to be transmitted and transmit it.
 */
void wmcApp::PrepareLanXSetLocoDriveAndTransmit(uint16_t Speed)
{
    Z21Slave::locInfo LocInfoTx;

    /* Get loc data and compose it for transmit */
    LocInfoTx.Speed = Speed;
    if (m_locLib.DirectionGet() == directionForward)
    {
        LocInfoTx.Direction = Z21Slave::locDirectionForward;
    }
    else
    {
        LocInfoTx.Direction = Z21Slave::locDirectionBackward;
    }

    LocInfoTx.Address = m_locLib.GetActualLocAddress();

    switch (m_locLib.DecoderStepsGet())
    {
    case decoderStep14: LocInfoTx.Steps = Z21Slave::locDecoderSpeedSteps14; break;
    case decoderStep28: LocInfoTx.Steps = Z21Slave::locDecoderSpeedSteps28; break;
    case decoderStep128: LocInfoTx.Steps = Z21Slave::locDecoderSpeedSteps128; break;
    }

    m_z21Slave.LanXSetLocoDrive(&LocInfoTx);
    WmcCheckForDataTx();
}
