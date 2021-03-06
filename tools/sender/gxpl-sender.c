/**
 * @file
 * Command Line xPL message sending tool
 *
 * Copyright 2015 (c), epsilonRT                
 * All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License")
 */
#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <gxPL.h>
#include "version-git.h"

/* constants ================================================================ */
#define DEFAULT_SOURCE "epsirt-xplsend.default"
#define DEFAULT_MSG_TYPE gxPLMessageCommand

/* public variables ========================================================= */
extern const char* __progname;

/* private functions ======================================================== */
static void prvPrintUsage (void);
static gxPLMessage * prvCreateMessage (int argc, char *argv[]);

/* main ===================================================================== */
int
main (int argc, char * argv[]) {
  int ret;
  gxPLApplication * app;
  gxPLDevice * device;
  gxPLSetting * setting;
  gxPLMessage * message;

  if (argc == 1) {

    prvPrintUsage ();
    exit (EXIT_SUCCESS);
  }

  setting = gxPLSettingFromCommandArgs (argc, argv, gxPLConnectViaHub);
  assert (setting);

  message = prvCreateMessage (argc, argv);

  // opens the xPL network
  app = gxPLAppOpen (setting);
  if (app == NULL) {

    fprintf (stderr, "Unable to start xPL");
    exit (EXIT_FAILURE);
  }

  // Create device so we can create messages
  if ( (device = gxPLAppAddDevice (app,
                                   gxPLMessageSourceVendorIdGet (message),
                                   gxPLMessageSourceDeviceIdGet (message),
                                   gxPLMessageSourceInstanceIdGet (message))) == NULL) {

    fprintf (stderr, "Unable to create xPL device\n");
    return false;
  }

  // Send the message
  if (setting->log > LOG_NOTICE) {
    char * str = gxPLMessageToString (message);
    printf ("<<< Transmitted message >>>\n%s>>> ", str);
    free (str);
  }
  if ( (ret = gxPLDeviceMessageSend (device, message)) < 0) {

    fprintf (stderr, "Unable to send xPL message\n");
    exit (EXIT_FAILURE);
  }
  if (setting->log > LOG_NOTICE) {
    printf ("Success, %d bytes transmitted\n ", ret);
  }

  if (gxPLAppClose (app) != 0) {

    fprintf (stderr, "Unable to close xPL network\n");
  }

  gxPLMessageDelete (message);
  return 0;
}

/* private functions ======================================================== */

// -----------------------------------------------------------------------------
// Parse command line for switches
// -s - source of message ident
// -t - target of message ident
// -m - message type
// -c - schema class / type
static gxPLMessage *
prvCreateMessage (int argc, char *argv[]) {
  int c;
  static char default_source[] = DEFAULT_SOURCE;
  char * source = default_source;
  char * target = NULL;
  char * schema_type = NULL;
  char * schema_class;
  char * str_msg_type = NULL;
  gxPLMessage * message = NULL;
  gxPLMessageType msg_type = DEFAULT_MSG_TYPE;
  gxPLId id;

  static const char short_options[] = "s:t:m:c:h" GXPL_GETOPT;
  static struct option long_options[] = {
    {"source",    required_argument, NULL, 's'},
    {"target",    required_argument, NULL, 't'},
    {"schema",    required_argument, NULL, 'c'},
    {"message",   required_argument, NULL, 'm'},
    {"help",     no_argument,        NULL, 'h' },
    {NULL, 0, NULL, 0} /* End of array need by getopt_long do not delete it*/
  };

  do  {

    c = getopt_long (argc, argv, short_options, long_options, NULL);

    switch (c) {

      case 's':
        source = optarg;
        PDEBUG ("set source to %s", source);
        break;

      case 't':
        target = optarg;
        PDEBUG ("set target to %s", target);
        break;

      case 'c':
        schema_type = optarg;
        PDEBUG ("set schema to %s", schema_type);
        break;

      case 'm':
        str_msg_type = optarg;
        PDEBUG ("set message to %s", str_msg_type);
        break;

      case 'h':
        prvPrintUsage();
        exit (EXIT_SUCCESS);
        break;

      default:
        break;
    }
  }
  while (c != -1);

  // Ensure we have a class
  if (schema_type == NULL) {

    fprintf (stderr, "The -c schema class.type is REQUIRED\n");
    exit (EXIT_FAILURE);
  }

  if (str_msg_type) {

    if (strcmp (str_msg_type, "stat") == 0) {

      msg_type = gxPLMessageStatus;
    }
    else if (strcmp (str_msg_type, "trig") == 0) {

      msg_type = gxPLMessageTrigger;
    }
  }

  // Create an appropriate message
  if ( (message = gxPLMessageNew (msg_type)) == NULL) {

    fprintf (stderr, "Unable to create message\n");
    exit (EXIT_FAILURE);
  }

  if (target) {

    if (gxPLIdFromString (&id, target) != 0) {

      fprintf (stderr, "Unable to set target\n");
      exit (EXIT_FAILURE);
    }
    gxPLMessageTargetIdSet (message, &id);
  }
  else {

    gxPLMessageBroadcastSet (message, true);
  }

  if (gxPLIdFromString (&id, source) != 0) {

    fprintf (stderr, "Unable to set source\n");
    exit (EXIT_FAILURE);
  }
  gxPLMessageSourceIdSet (message, &id);


  schema_class = strsep (&schema_type, ".");
  if (schema_type == NULL) {

    fprintf (stderr, "Unable to set schema\n");
    exit (EXIT_FAILURE);
  }

  if (gxPLMessageSchemaSet (message, schema_class, schema_type) != 0) {

    fprintf (stderr, "Unable to set schema\n");
    exit (EXIT_FAILURE);
  }

  if (optind < argc) {
    char * name;
    char * value;

    while (optind < argc) {

      value = argv[optind++];
      name = strsep (&value, "=");

      if (value != NULL) {

        gxPLMessagePairAdd (message, name, value);
      }
    }
  }

  return message;
}

// -----------------------------------------------------------------------------
// Print usage info
static void
prvPrintUsage (void) {
  printf ("%s - xPL Message Sender\n", __progname);
  printf ("Copyright (c) 2015-2016 epsilonRT                \n\n");
  printf ("Usage: %s [-i interface] [-n network] [-W timeout] [-s source] [-t target]"
          " [-m message_type] [options] -c schema  name=value name=value ...\n", __progname);
  printf ("  -i interface - use interface named interface (i.e. eth0)"
          " as network interface\n");
  printf ("  -n iolayer   - use hardware abstraction layer to access the network"
          " (i.e. udp, xbeezb... default: udp)\n");
  printf ("  -W timeout   - set the timeout at the opening of the io layer\n");
  printf ("  -s source    - source of message in vendor-device.instance"
          " (default: " DEFAULT_SOURCE ")\n");
  printf ("  -t target    - target of message in vendor-device.instance format."
          "  (default: broadcast)\n");
  printf ("  -m type      - message type: cmnd, trig or stat (default: cmnd)\n");
  printf ("  -c schema    - schema class and type formatted as class.type"
          " - REQUIRED\n\n");
  printf ("  -B baudrate  - set serial baudrate (if iolayer use serial port)\n");
  printf ("  -r           - performed iolayer reset (if supported)\n");
  printf ("  -d           - enable debugging, it can be doubled or tripled to"
          " increase the level of debug. \n");
  printf ("  -h           - print this message\n");
}

/* ========================================================================== */
