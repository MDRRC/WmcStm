/***********************************************************************************************************************
   @file   wmc_app.cpp
   @brief  Main application of WifiManualControl (WMC).
 **********************************************************************************************************************/

#ifndef EEP_CFG_H
#define EEP_CFG_H

/***********************************************************************************************************************
   I N C L U D E S
 **********************************************************************************************************************/

/***********************************************************************************************************************
   C L A S S E S
 **********************************************************************************************************************/

class EepCfg
{
public:
    static const uint8_t EepromVersion = 9; /* Version of data in EEPROM. */

    static const int EepromVersionAddress         = 1;   /* EEPROM address version info. */
    static const int AcTypeControlAddress         = 2;   /* EEPROM address for "AC" type control */
    static const int EmergencyStopEnabledAddress  = 4;   /* EEPROM address for emergency stop option */
    static const int StaticIpAddress              = 6;   /* EEPROM address static or dynamic IP address */
    static const int ButtonAdcValuesAddressValid  = 8;   /* EEPROM address for valid ADC data indicator. */
    static const int PulseSwitchInvertAddress     = 10;  /* EEPORM address for inverting pulse switch direction. */
    static const int AutoOffAddress               = 12;  /* EEPORM address for turnout off option enabled/disabled. */
    static const int ButtonAdcValuesAddress       = 20;  /* EEPORM address for ADC data of buttons. */
    static const int SelectedLocAddress           = 48;  /* EEPORM address for storage of selected locomotive. */
    static const int SsidNameAddress              = 50;  /* EEPROM Address of Ssid name */
    static const int SsidPasswordAddress          = 100; /* EEPROM Address of Ssid password */
    static const int EepIpAddressZ21              = 165; /* EEPROM Address of IP address of Z21. */
    static const int EepIpAddressWmc              = 169; /* EEPROM Address of IP address of Wmc. */
    static const int EepIpSubnet                  = 173; /* EEPROM Address of IP subnet of Wmc. */
    static const int EepIpGateway                 = 177; /* EEPROM Address of gateway ip. */
    static const int locLibEepromAddressNumOfLocs = 181; /* EEPROM address num of locs. */
    static const int locLibEepromAddressData      = 185; /* Start in EEPROM address loc data. */
};
#endif
