/**
 **********************************************************************************************************************
 * @file  wmc_event.h
 * @brief Collection of event data and events fo the WMC application.
 ***********************************************************************************************************************
 */
#ifndef WMC_EVENT_H
#define WMC_EVENT_H

/***********************************************************************************************************************
 * I N C L U D E S
 **********************************************************************************************************************/
#include <tinyfsm.hpp>

/***********************************************************************************************************************
 * T Y P E D  E F S  /  E N U M
 **********************************************************************************************************************/

/**
 * Status of the pulse switch.
 */
enum pulseSwitchStatus
{
    turn = 0,
    pushturn,
    pushedShort,
    pushedNormal,
    pushedlong,
    released,
};

/**
 * Definitions for the push buttons.
 */
enum pushButtons
{
    button_0 = 0,
    button_1,
    button_2,
    button_3,
    button_4,
    button_5,
    button_power,
    button_none
};

/**
 * CV programming module events.
 */
enum cvProgRequest
{
    cvRead = 0,
    cvWrite,
    cvStatusRequest,
    pomWrite,
    cvExit,
};

/**
 * Pulse switch event.
 */
struct pulseSwitchEvent : tinyfsm::Event
{
    int8_t Delta;             /* Delta of pulsw switch. */
    pulseSwitchStatus Status; /* Status */
};

/**
 * Event for buttons.
 */
struct pushButtonsEvent : tinyfsm::Event
{
    pushButtons Button; /* Button which was pressed. */
};

/**
 * 5msec Update event
 */
struct updateEvent5msec : tinyfsm::Event
{
};

/**
 * 50msec Update event
 */
struct updateEvent50msec : tinyfsm::Event
{
};

/**
 * 100msec Update event
 */
struct updateEvent100msec : tinyfsm::Event
{
};

/**
 * 500msec Update event
 */
struct updateEvent500msec : tinyfsm::Event
{
};

/**
 * 3 Seconds ipdate event
 */
struct updateEvent3sec : tinyfsm::Event
{
};

/**
 * Data changed by command line interface event.
 */
struct cliEnterEvent : tinyfsm::Event
{
};

/**
 * CV programming events from cv module.
 */
struct cvProgEvent : tinyfsm::Event
{
    cvProgRequest Request;
    uint16_t Address;
    uint16_t CvNumber;
    uint8_t CvValue;
};

/***********************************************************************************************************************
 * C L A S S E S
 **********************************************************************************************************************/

#endif
