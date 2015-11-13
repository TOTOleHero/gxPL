/**
 * @file
 * gxPL Core test
 *
 * Copyright 2015 (c), Pascal JEAN aka epsilonRT
 * All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License")
 */
#include <stdio.h>
#include <stdlib.h>
#include <gxPL.h>
#include "version-git.h"

/* constants ================================================================ */
#define HBEAT_INTERVAL 5

static const gxPLId source = {
  .vendor = "xpl",
  .device = "xplhal",
  .instance = "myhouse"
};

/* macros =================================================================== */
#define test(t) do { \
    if (!(t)) { \
      fprintf_P (stderr, PSTR("line %d in %s: %d failed !\n"),  __LINE__, \
               __FUNCTION__, test_count); \
      exit (-1); \
    } \
  } while (0)

/* private variables ======================================================== */
static gxPLApplication * app;
static gxPLMessage * message;
static bool hasHub;
static int test_count;
static int msg_count;

/* private functions ======================================================== */
static void prvSignalHandler (int sig) ;
static void prvMessageHandler (gxPLApplication * app, gxPLMessage * msg, void * p);
static void prvHeartbeatMessageNew (void);

/* main ===================================================================== */
int
main (int argc, char **argv) {
  int ret = 0;
  xVector * iolist;
  gxPLSetting * setting;
  char hello[] = ".";

  // Gets the available io layer list
  test_count++;
  iolist = gxPLIoLayerList();
  test (iolist);

  // retrieved the requested configuration from the command line
  test_count++;
  setting = gxPLSettingNew ("s0", "xbeezb", gxPLConnectViaHub);
  test (setting);

  // verify that the requested io layer is available
  test_count++;
  ret = iVectorFindFirstIndex (iolist, setting->iolayer);
  test (ret >= 0);

  // opens the xPL network
  test_count++;
  vVectorDestroy (iolist);
  app = gxPLAppOpen (setting);
  test (app);

  // adds a message listener
  test_count++;
  ret = gxPLMessageListenerAdd (app, prvMessageHandler, hello);
  prvHeartbeatMessageNew();

  // View network information
  printf ("Starting test service on %s...\n", gxPLIoInterfaceGet (app));
  printf ("  listen on  %s", gxPLIoLocalAddrGet (app));
  if (gxPLIoInfoGet (app)->port >= 0) {
    
    printf (":%d\n", gxPLIoInfoGet (app)->port);
  }
  else {
    
    putchar ('\n');
  }
  printf ("  bcast  on  %s\n", gxPLIoBcastAddrGet (app));
  printf ("Press Ctrl+C to abort ...\n");


  for (;;) {

    // Main loop
    ret = gxPLAppPoll (app, 100);
    test (ret == 0);
    fflush (stdout);
  }

  return 0;
}


/* private functions ======================================================== */
#if 0
// -----------------------------------------------------------------------------
// signal handler
static void
prvSignalHandler (int sig) {
  int ret;

  switch (sig) {

    case SIGALRM:
      msg_count++;
      printf ("\n\n******** Sending message[%d] ********\n", msg_count);
      ret = gxPLAppBroadcastMessage (app, message);
      test (ret > 0);
      if (hasHub) {

        alarm (HBEAT_INTERVAL * 60 - 10);
      }
      else {

        alarm (10);
      }
      break;

    case SIGTERM:
    case SIGINT:
      // Delete the message
      gxPLMessageDelete (message);

      test_count++;
      ret = gxPLAppClose (app);
      test (ret == 0);

      printf ("\neverything was closed.\nHave a nice day !\n");
      exit (EXIT_SUCCESS);
      break;
  }

}
#endif

// -----------------------------------------------------------------------------
// message handler
static void
prvMessageHandler (gxPLApplication * app, gxPLMessage * msg, void * p) {

  // See if we need to check the message for hub detection
  if ( (hasHub == false) && (gxPLAppIsHubEchoMessage (app, msg, NULL) == true)) {

    hasHub = true;
    printf ("\n******** Hub detected and confirmed existing ********\n");
  }
}

// -----------------------------------------------------------------------------
// Create Heartbeat message
static void
prvHeartbeatMessageNew (void) {
  int ret;

  test_count++;
  message = gxPLMessageNew (gxPLMessageStatus);
  test (message);

  test_count++;
  ret = gxPLMessageSourceIdSet (message, &source);
  test (ret == 0);

  test_count++;
  ret = gxPLMessageBroadcastSet (message, true);
  test (ret == 0);

  test_count++;
  ret = gxPLMessageSchemaClassSet (message, "hbeat");
  test (ret == 0);

  if (gxPLIoInfoGet (app)->family & gxPLNetFamilyInet)  {

    ret = gxPLMessageSchemaTypeSet (message, "app");
  }
  else {

    ret = gxPLMessageSchemaTypeSet (message, "basic");
  }

  test_count++;
  ret = gxPLMessagePairAddFormat (message, "interval", "%d", HBEAT_INTERVAL);
  test (ret == 0);

  if (gxPLIoInfoGet (app)->family & gxPLNetFamilyInet)  {

    test_count++;
    ret = gxPLMessagePairAddFormat (message, "port", "%d", gxPLIoInfoGet (app)->port);
    test (ret == 0);

    test_count++;
    ret = gxPLMessagePairAdd (message, "remote-ip", gxPLIoLocalAddrGet (app));
    test (ret == 0);
  }

  test_count++;
  ret = gxPLMessagePairAdd (message, "version", VERSION_SHORT);
  test (ret == 0);
}

/* ========================================================================== */