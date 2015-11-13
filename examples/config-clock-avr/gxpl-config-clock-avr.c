/**
 * @file
 * Simple configurable xPL device device that sends a time update periodically
 *
 * Copyright 2005 (c), Gerald R Duprey Jr
 * Copyright 2015 (c), Pascal JEAN aka epsilonRT
 * All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avrio/task.h>
#include <gxPL.h>
#include "version-git.h"

/* constants ================================================================ */
#define CLOCK_VERSION VERSION_SHORT
#define DEFAULT_TICK_RATE 60
#define TICK_RATE_CFG_NAME "tickrate"
#define STARTED_CFG_NAME "started"
#define REPORT_OWN_MESSAGE    true
#define DEFAULT_CONFIG_FILE "gxpl-clock.xpl"

/* private variables ======================================================== */
static gxPLApplication * app;
static gxPLDevice * device;
static gxPLMessage * message;

/* configurable items ======================================================= */
static int tick_rate;  // Second between ticks */
static bool started;

/* private functions ======================================================== */
static void prvSendTick (void);
static void prvMessageListener (gxPLDevice * device, gxPLMessage * msg, void * udata) ;
static const char * prvIntToStr (int value);
static void prvSetConfig (gxPLDevice * device);
static void prvConfigChanged (gxPLDevice * device, void * udata);

/* main ===================================================================== */
int
main (void) {
  int ret;
  gxPLSetting * setting;

  setting = gxPLSettingNew ("s0", "xbeezb", gxPLConnectViaHub);
  assert (setting);

  // opens the xPL network
  app = gxPLAppOpen (setting);
  if (app == NULL) {

    fprintf (stderr, "Unable to start xPL");
    exit (EXIT_FAILURE);
  }

  // Create a configurable device and set our application version
  device = gxPLAppAddConfigurableDevice (app, "epsirt", "clock",
                                         gxPLConfigPath (DEFAULT_CONFIG_FILE));
  assert (device);

  ret = gxPLDeviceVersionSet (device, CLOCK_VERSION);
  assert (ret == 0);

  ret = gxPLDeviceReportOwnMessagesSet (device, REPORT_OWN_MESSAGE);
  assert (ret == 0);

  // If the configuration was not reloaded, then this is our first time and
  // we need to define what the configurables are and what the default values
  // should be.
  if (gxPLDeviceIsConfigured (device) == false) {

    // Define a configurable item and give it a default
    ret = gxPLDeviceConfigItemAdd (device, TICK_RATE_CFG_NAME, gxPLConfigReconf, 1);
    assert (ret == 0);
    ret = gxPLDeviceConfigValueSet (device, TICK_RATE_CFG_NAME, prvIntToStr (DEFAULT_TICK_RATE));
    assert (ret == 0);
    ret = gxPLDeviceConfigItemAdd (device, STARTED_CFG_NAME, gxPLConfigReconf, 1);
    assert (ret == 0);
    ret = gxPLDeviceConfigValueSet (device, STARTED_CFG_NAME, "on");
    assert (ret == 0);
  }

  // Parse the device configurables into a form this program
  // can use (whether we read a setting or not)
  prvSetConfig (device);

  // Add a responder for time setting
  ret = gxPLDeviceListenerAdd (device, prvMessageListener, gxPLMessageAny,
                               "clock", NULL, NULL);
  assert (ret == 0);

  // Add a device change listener we'll use to pick up a new tick rate
  ret = gxPLDeviceConfigListenerAdd (device, prvConfigChanged, NULL);
  assert (ret == 0);

  // Create a message to send
  message = gxPLDeviceMessageNew (device, gxPLMessageStatus);
  assert (message);
  // Setting up the message
  ret = gxPLMessageBroadcastSet (message, true);
  assert (ret == 0);
  ret = gxPLMessageSchemaSet (message, "clock", "update");
  assert (ret == 0);

  // Enable the service
  ret = gxPLDeviceEnable (device, true);
  assert (ret == 0);

  for (;;) {
    // Let XPL run for a while, returning after it hasn't seen any
    // activity in 100ms or so
    ret = gxPLAppPoll (app, 100);
    assert (ret == 0);

    // Process clock tick update checking
    prvSendTick();
  }
  return 0;
}


/* private functions ======================================================== */

// --------------------------------------------------------------------------
//  Quickly to convert an integer to string */
static const char *
prvIntToStr (int value) {
  static char numBuffer[10];

  sprintf (numBuffer, "%d", value);
  return numBuffer;
}

// --------------------------------------------------------------------------
//  It's best to put the logic for reading the device configuration
//  and parsing it into your code in a seperate function so it can
//  be used by your prvConfigChanged and your startup code that
//  will want to parse the same data after a setting file is loaded */
static void
prvSetConfig (gxPLDevice * device) {

  // Get the tickrate */
  const char * str_rate = gxPLDeviceConfigValueGet (device, TICK_RATE_CFG_NAME);
  const char * str_started = gxPLDeviceConfigValueGet (device, STARTED_CFG_NAME);
  int new_rate;
  char * endptr;

  // Handle bad configurable (override it) */
  if ( (str_rate == NULL) || (strlen (str_rate) == 0)) {
    gxPLDeviceConfigValueSet (device, TICK_RATE_CFG_NAME, prvIntToStr (tick_rate));
    return;
  }

  // Convert text to a number */
  new_rate = strtol (str_rate, &endptr, 10);

  if (*endptr != '\0') {
    // Bad value -- override it */
    gxPLDeviceConfigValueSet (device, TICK_RATE_CFG_NAME, prvIntToStr (tick_rate));
    return;
  }

  if (strcmp (str_started, "on") == 0) {
    started = true;
  }
  else if (strcmp (str_started, "off") == 0) {
    started = false;
  }

  // Install new tick rate */
  tick_rate = new_rate;
}

// --------------------------------------------------------------------------
//  Handle a change to the device device configuration */
static void
prvConfigChanged (gxPLDevice * device, void * udata) {

  // Read setting items for device and install */
  prvSetConfig (device);
}

// -----------------------------------------------------------------------------
// message handler
static void
prvMessageListener (gxPLDevice * device, gxPLMessage * msg, void * udata) {

  printf ("Received a Clock Message from %s-%s.%s of type %d for %s.%s\n",
          gxPLMessageSourceVendorIdGet (msg),
          gxPLMessageSourceDeviceIdGet (msg),
          gxPLMessageSourceInstanceIdGet (msg),
          gxPLMessageTypeGet (msg),
          gxPLMessageSchemaClassGet (msg),
          gxPLMessageSchemaTypeGet (msg));
}

// -----------------------------------------------------------------------------
static void
prvSendTick (void) {

  if ( (tick_rate > 0) && (started))  {
    static unsigned long last;
    unsigned long now;

    now = gxPLTime ();

    // Skip unless the delay has passed
    if ( (last == 0) || ( (now - last) >= tick_rate)) {

      // Install the value and send the message
      gxPLMessagePairSet (message, "time", gxPLTimeStr(now));

      // Broadcast the message
      gxPLDeviceMessageSend (device, message);

      // And reset when we last sent the clock update
      last = now;
    }
  }
}

/* ========================================================================== */