/**
 * @file
 * xPL Message support functions
 *
 * Copyright 2015 (c), epsilonRT                
 * All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License")
 */
#include "config.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <gxPL/util.h>
#include "message_p.h"

/* constants ================================================================ */
#ifndef DEFAULT_ALLOC_STR_GROW
#define DEFAULT_ALLOC_STR_GROW  256
#endif

/* private functions ======================================================== */

// -----------------------------------------------------------------------------
static int
prvStrPrintf (char ** buf, int * buf_size, int index, const char * format, ...) {
  va_list ap;
  int buf_free = *buf_size - index - 1;
  int size;

  va_start (ap, format);
  // trying to write the string in the buffer...
  size = vsnprintf (& (*buf) [index], buf_free, format, ap);

  while (size >= buf_free) {

    // the buffer is too small, it reallocates memory...
    *buf_size += DEFAULT_ALLOC_STR_GROW;
    buf_free  += DEFAULT_ALLOC_STR_GROW;
    *buf = realloc (*buf, *buf_size);
    assert (buf);

    //  and try again !
    va_end (ap);
    va_start (ap, format);
    size = vsnprintf (& (*buf) [index], buf_free, format, ap);
  }

  va_end (ap);
  return size;
}

// -----------------------------------------------------------------------------
static gxPLPair *
prvPairFromLine (char ** line) {
  char * p = strsep (line, "\n");

  if (*line) {
    // line found
    gxPLPair * pair = gxPLPairFromString (p);
    if (pair) {
      return pair;
    }
    PWARNING ("unable to find a '=' in this line: %s", p);
  }
  return NULL;
}


/* internal public functions ================================================ */

// -----------------------------------------------------------------------------
gxPLMessage *
gxPLMessageFromString (gxPLMessage * m, char * str) {
  char *p, *line;
  gxPLPair * pair;

  if (strlen (str) == 0) {

    PDEBUG ("empty message");
    return m;
  }

  if (m == NULL) {
    // New message
    m = gxPLMessageNew (gxPLMessageAny);
    assert (m);
    m->isreceived = 1;
  }

  line = str;
  while ( (m->isreceived) && (m->isvalid == 0) &&
          (m->iserror == 0) && (line != NULL)) {

    // message received and not yet decoded
    switch (m->state) {

      case gxPLMessageStateInit:
        //--------------------------------------------------------------------

        // gets first line
        p = strsep (&line, "\n");

        if ( (strncmp (p, "xpl-", 4) != 0) ||
             (line == NULL) || (strlen (p) != 8)) {

          // The string does not contain a xPL Message !
          PWARNING ("Unknown message header '%s' - bad message", p);
          m->iserror = 1;
          break;
        }

        if (strcmp (&p[4], "cmnd") == 0) {

          m->type = gxPLMessageCommand;
        }
        else if (strcmp (&p[4], "stat") == 0) {

          m->type = gxPLMessageStatus;
        }
        else if (strcmp (&p[4], "trig") == 0) {

          m->type = gxPLMessageTrigger;
        }
        else {

          PWARNING ("Unknown message type '%s' - bad message", p);
          m->iserror = 1;
          break;
        }
        m->state = gxPLMessageStateHeader;
        // no break;

      case gxPLMessageStateHeader:
        //--------------------------------------------------------------------
        // gets next line
        p = strsep (&line, "\n");
        if (line == NULL) {
          // nothing to read, exit...
          break;
        }

        if (strcmp (p, "{") != 0) {

          PWARNING ("incorrectly formatted message", p);
          m->iserror = 1;
          break;
        }
        m->state = gxPLMessageStateHeaderHop;
        // no break;

      case gxPLMessageStateHeaderHop:
        //--------------------------------------------------------------------
        // gets next pair
        pair = prvPairFromLine (&line);

        if (line == NULL) {
          // no line found or ...
          if (pair == NULL) {
            // no '=' found
            m->iserror = 1;
          }
          break;
        }

        if (strcmp (pair->name, "hop") == 0) {
          int hop;
          char *endptr;

          hop = strtol (pair->value, &endptr, 10);
          if (*endptr != '\0') {

            // invalid hop value
            gxPLPairDelete (pair);
            PWARNING ("invalid hop count");
            m->iserror = 1;
            break;
          }
          gxPLPairDelete (pair);
          m->hop = hop;
          m->state = gxPLMessageStateHeaderSource;
        }
        else {

          PWARNING ("unable to find hop count");
          m->iserror = 1;
          break;
        }
        // no break;

      case gxPLMessageStateHeaderSource:
        //--------------------------------------------------------------------
        // gets next pair
        pair = prvPairFromLine (&line);

        if (line == NULL) {
          // no line found or ...
          if (pair == NULL) {
            // no '=' found
            m->iserror = 1;
          }
          break;
        }

        if (strcmp (pair->name, "source") == 0) {

          if (gxPLIdFromString (&m->source, pair->value) != 0) {

            // illegal source value
            gxPLPairDelete (pair);
            PWARNING ("invalid source");
            m->iserror = 1;
            break;
          }

          gxPLPairDelete (pair);
          m->state = gxPLMessageStateHeaderTarget;
        }
        else {

          PWARNING ("unable to find source");
          m->iserror = 1;
          break;
        }
        // no break;

      case gxPLMessageStateHeaderTarget:
        //--------------------------------------------------------------------
        // gets next pair
        pair = prvPairFromLine (&line);

        if ( (line == NULL) || (pair == NULL)) {
          // no line found or no '=' found
          m->iserror = 1;
          break;
        }

        if (strcmp (pair->name, "target") == 0) {

          if (strcmp (pair->value, "*") == 0) {

            strcpy (m->target.vendor, "*");
            m->isbroadcast = 1;
          }
          else {

            if (gxPLIdFromString (&m->target, pair->value) != 0) {

              // illegal target value
              PWARNING ("invalid target");
              gxPLPairDelete (pair);
              break;
            }
            if ( (strcmp (m->target.vendor, "xpl") == 0) &&
                 (strcmp (m->target.device, "group") == 0)) {

              // this message is for a group, the group is stored in the
              // instance of the target.
              m->isgrouped = 1;
            }
          }

          gxPLPairDelete (pair);
          m->state = gxPLMessageStateHeaderEnd;
        }
        else {

          PWARNING ("unable to find target");
          m->iserror = 1;
          break;
        }
        // no break;

      case gxPLMessageStateHeaderEnd:
        //--------------------------------------------------------------------
        // gets next line
        p = strsep (&line, "\n");
        if (line == NULL) {
          // nothing to read, exit...
          break;
        }

        if (strcmp (p, "}") != 0) {

          PWARNING ("incorrectly formatted message", p);
          m->iserror = 1;
          break;
        }
        m->state = gxPLMessageStateSchema;
        // no break;

      case gxPLMessageStateSchema:
        //--------------------------------------------------------------------
        // gets next line
        p = strsep (&line, "\n");
        if (line) {
          char * class;
          char * type = p;

          class = strsep (&type, ".");
          if (type == NULL) {

            PWARNING ("unable to find a '.' in this line: %s", p);
            m->iserror = 1;
            break;
          }
          if (gxPLMessageSchemaClassSet (m, class) != 0) {

            PWARNING ("invalid schema class");
            m->iserror = 1;
            break;
          }
          if (gxPLMessageSchemaTypeSet (m, type) != 0) {

            PWARNING ("invalid schema type");
            m->iserror = 1;
            break;
          }
          m->state = gxPLMessageStateBodyBegin;
        }
        else {

          // nothing to read, exit...
          break;
        }
        // no break;

      case gxPLMessageStateBodyBegin:
        //--------------------------------------------------------------------
        // gets next line
        p = strsep (&line, "\n");
        if (line == NULL) {
          // nothing to read, exit...
          break;
        }

        if (strcmp (p, "{") != 0) {

          PWARNING ("Message improperly formatted: %s", p);
          m->iserror = 1;
          break;
        }
        m->state = gxPLMessageStateBody;
        // no break;

      case gxPLMessageStateBody:
        //--------------------------------------------------------------------
        // gets next pair
        if (strchr (line, '=')) {
          pair = prvPairFromLine (&line);

          if (pair) {
            if (iVectorAppend (&m->body, pair) == 0) {

              break;
            }
            gxPLPairDelete (pair);
          }
          m->iserror = 1;
          PWARNING ("unable to append a pair in the message body");
          break;
        }
        else {

          m->state = gxPLMessageStateBodyEnd;
        }
        // no break;

      case gxPLMessageStateBodyEnd:
        //--------------------------------------------------------------------
        // gets next line
        p = strsep (&line, "\n");
        if (line == NULL) {
          // nothing to read, exit...
          break;
        }

        if (strcmp (p, "}") != 0) {

          PWARNING ("Message improperly formatted: %s", p);
          m->iserror = 1;
          break;
        }
        m->state = gxPLMessageStateEnd;
        m->isvalid = 1;
        break;

      default:
        break;
    } // switch end
  } // while end

  if (m->iserror) {

    m->state = gxPLMessageStateError;
  }

  return m;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageTypeToString (gxPLMessageType type) {

  switch (type) {
    case gxPLMessageAny:
      return "*"; // only for filter purpose

    case gxPLMessageCommand:
      return  "xpl-cmnd";

    case gxPLMessageStatus:
      return  "xpl-stat";

    case gxPLMessageTrigger:
      return  "xpl-trig";

    default:
      break;
  }
  return NULL;
}

// -----------------------------------------------------------------------------
gxPLMessageType
gxPLMessageTypeFromString (const char * str) {

  if (strcmp (str, "*") == 0) {
    return gxPLMessageAny;
  }
  else if (strncmp (str, "xpl-cmnd", 8) == 0) {
    return gxPLMessageCommand;
  }
  else if (strncmp (str, "xpl-stat", 8) == 0) {
    return gxPLMessageStatus;
  }
  else if (strncmp (str, "xpl-trig", 8) == 0) {
    return gxPLMessageTrigger;
  }
  PERROR ( "Unknown message type");
  return -1;
}

// -----------------------------------------------------------------------------
char *
gxPLMessageToString (const gxPLMessage * message) {
  char * buf;
  const char * str;
  int index = 0;
  int buf_size = DEFAULT_ALLOC_STR_GROW;

  buf = malloc (buf_size);
  assert (buf);


  // Write the header block
  str = gxPLMessageTypeToString (message->type);
  if (str == NULL) {

    PERROR (
          "Unable to format message -- invalid/unknown message type %d",
          message->type);
    free (buf);
    return NULL;
  }

  // Writes message type and begins the header block
  index += prvStrPrintf (&buf, &buf_size, index, "%s\n{\nhop=%d\n",
                         str, message->hop);
  // Writes the source
  const gxPLId * n = gxPLMessageSourceIdGet (message);
  index += prvStrPrintf (&buf, &buf_size, index, "source=%s-%s.%s\n",
                         n->vendor, n->device, n->instance);
  // Writes the target and ends the header
  if (message->isbroadcast) {

    index += prvStrPrintf (&buf, &buf_size, index, "target=*\n}\n");
  }
  else {

    n = gxPLMessageTargetIdGet (message);
    index += prvStrPrintf (&buf, &buf_size, index, "target=%s-%s.%s\n}\n",
                           n->vendor, n->device, n->instance);
  }

  // Writes the schema and begins the body
  const gxPLSchema * s = gxPLMessageSchemaGet (message);
  index += prvStrPrintf (&buf, &buf_size, index, "%s.%s\n{\n", s->class, s->type);

  // Writes the name/value pairs (body)
  const xVector * body = gxPLMessageBodyGetConst (message);
  for (int i = 0; i < iVectorSize (body); i++) {
    const gxPLPair * p = (const gxPLPair *) pvVectorGet (body, i);

    index += prvStrPrintf (&buf, &buf_size, index, "%s=%s\n", p->name, p->value);
  }

  // Ends the body and message
  index += prvStrPrintf (&buf, &buf_size, index, "}\n", s->class, s->type);
  // shorten the memory block to the strict minimum
  buf = realloc (buf, index + 1);
  return buf;
}

// -----------------------------------------------------------------------------
gxPLMessage *
gxPLMessageNew (gxPLMessageType type) {

  gxPLMessage * message = calloc (1, sizeof (gxPLMessage));
  assert (message);

  if (iVectorInit (&message->body, 3, NULL, gxPLPairDelete) == 0) {

    if (iVectorInitSearch (&message->body, gxPLPairKey, gxPLPairMatch) == 0) {

      message->hop = 1;
      message->type = type;
      message->state = gxPLMessageStateInit;
      return message;
    }
  }
  free (message);
  return NULL;
}

// -----------------------------------------------------------------------------
void
gxPLMessageDelete (gxPLMessage * message) {

  if (message) {
    vVectorDestroy (&message->body);
    free (message);
  }
}

// -----------------------------------------------------------------------------
int
gxPLMessageSchemaClassSet (gxPLMessage * message, const char * schema_class) {

  return gxPLSchemaClassSet (&message->schema, schema_class);
}

// -----------------------------------------------------------------------------
int
gxPLMessageSchemaTypeSet (gxPLMessage * message, const char * schema_type) {

  return gxPLSchemaTypeSet (&message->schema, schema_type);
}

// -----------------------------------------------------------------------------
int
gxPLMessageSourceVendorIdSet (gxPLMessage * message, const char * vendor_id) {

  return gxPLIdVendorIdSet (&message->source, vendor_id);
}

// -----------------------------------------------------------------------------
int
gxPLMessageSourceDeviceIdSet (gxPLMessage * message, const char * device_id) {

  return gxPLIdDeviceIdSet (&message->source, device_id);
}

// -----------------------------------------------------------------------------
int
gxPLMessageSourceInstanceIdSet (gxPLMessage * message, const char * instance_id) {

  return gxPLIdInstanceIdSet (&message->source, instance_id);
}


// -----------------------------------------------------------------------------
int
gxPLMessageTargetVendorIdSet (gxPLMessage * message, const char * vendor_id) {

  return gxPLIdVendorIdSet (&message->target, vendor_id);
}

// -----------------------------------------------------------------------------
int
gxPLMessageTargetDeviceIdSet (gxPLMessage * message, const char * device_id) {

  return gxPLIdDeviceIdSet (&message->target, device_id);
}

// -----------------------------------------------------------------------------
int
gxPLMessageTargetInstanceIdSet (gxPLMessage * message, const char * instance_id) {

  return gxPLIdInstanceIdSet (&message->target, instance_id);
}

// -----------------------------------------------------------------------------
int
gxPLMessagePairAdd (gxPLMessage * message, const char * name, const char * value) {

  if ( (name) && (message)) {
    if (strlen (name) > GXPL_NAME_MAX) {

      errno = EINVAL;
    }
    else {
      gxPLPair * p = calloc (1, sizeof (gxPLPair));
      assert (p);

      p->name = malloc (strlen (name) + 1);
      assert (p->name);
      if (gxPLStrCpy (p->name, name) > 0) {

        if (value == NULL) {

          value = "";
        }
        p->value = malloc (strlen (value) + 1);
        assert (p->value);

        strcpy (p->value, value);
        if (iVectorAppend (&message->body, p) == 0) {

          return 0;
        }
      }
      gxPLPairDelete (p);
    }
  }
  errno = EFAULT;
  return -1;
}


// -----------------------------------------------------------------------------
int
gxPLMessagePairAddFormat (gxPLMessage * message, const char * name,
                          const char * format, ...) {
  va_list ap;

  va_start (ap, format);
  int size = vsnprintf (NULL, 0, format, ap);
  va_end (ap);

  char * value = malloc (size + 1);
  assert (value);

  va_start (ap, format);
  vsprintf (value, format, ap);
  va_end (ap);

  int ret = gxPLMessagePairAdd (message, name, value);
  free (value);

  return ret;
}

// -----------------------------------------------------------------------------
int
gxPLMessagePairSet (gxPLMessage * message, const char * name,
                    const char * value) {

  if ( (name) && (message)) {
    if (strlen (name) > GXPL_NAME_MAX) {

      errno = EINVAL;
    }
    else {
      gxPLPair * p = pvVectorFindFirst (&message->body, name);

      if (p == NULL) {

        return gxPLMessagePairAdd (message, name, value);
      }

      if (strcmp (p->name, name) != 0) {

        if (strlen (p->name) != strlen (name)) {

          p->name = realloc (p->name, strlen (name) + 1);
          assert (p->name);
        }

        if (gxPLStrCpy (p->name, name) < 0) {

          return -1;
        }
      }

      if (value == NULL) {

        value = "";
      }

      if (strcmp (p->value, value) != 0) {
        if (strlen (p->value) != strlen (value)) {

          p->value = realloc (p->value, strlen (value) + 1);
          assert (p->value);
        }

        strcpy (p->value, value);
        return 0;
      }
    }
  }
  errno = EFAULT;
  return -1;
}

// -----------------------------------------------------------------------------
int
gxPLMessagePairSetFormat (gxPLMessage * message, const char * name,
                          const char * format, ...) {
  va_list ap;

  va_start (ap, format);
  int size = vsnprintf (NULL, 0, format, ap);
  va_end (ap);

  char * value = malloc (size + 1);
  assert (value);

  va_start (ap, format);
  vsprintf (value, format, ap);
  va_end (ap);

  int ret = gxPLMessagePairSet (message, name, value);
  free (value);

  return ret;
}

// -----------------------------------------------------------------------------
int
gxPLMessagePairValuesSet (gxPLMessage * message, ...) {
  va_list ap;
  char * name;
  char * value;

  va_start (ap, message);
  for (;;) {

    if ( (name = va_arg (ap, char *)) == NULL) {

      break;
    }

    value = va_arg (ap, char *);

    if (gxPLMessagePairSet (message, name, value) != 0) {

      return -1;
    }
  }
  va_end (ap);
  return 0;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessagePairGet (const gxPLMessage * message, const char * name) {

  gxPLPair * p = (gxPLPair *) pvVectorFindFirst (&message->body, name);
  if (p) {
    return p->value;
  }

  return NULL;
}

// -----------------------------------------------------------------------------
int
gxPLMessagePairExist (const gxPLMessage * message, const char * name) {

  return gxPLMessagePairGet (message, name) != NULL;
}

// -----------------------------------------------------------------------------
int
gxPLMessageSourceSet (gxPLMessage * message, const char * vendor_id,
                      const char * device_id, const char * instance_id) {
  gxPLId id;
  strcpy (id.vendor, vendor_id);
  strcpy (id.device, device_id);
  strcpy (id.instance, instance_id);

  return gxPLMessageSourceIdSet (message, &id);
}

// -----------------------------------------------------------------------------
gxPLMessageType
gxPLMessageTypeGet (const gxPLMessage * message) {

  return message->type;
}

// -----------------------------------------------------------------------------
int
gxPLMessageHopGet (const gxPLMessage * message) {

  return message->hop;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageTargetVendorIdGet (const gxPLMessage * message) {

  return message->target.vendor;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageTargetDeviceIdGet (const gxPLMessage * message) {

  return message->target.device;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageTargetInstanceIdGet (const gxPLMessage * message) {

  return message->target.instance;
}

// -----------------------------------------------------------------------------
const gxPLId *
gxPLMessageSourceIdGet (const gxPLMessage * message) {

  return &message->source;
}

// -----------------------------------------------------------------------------
const gxPLId *
gxPLMessageTargetIdGet (const gxPLMessage * message) {

  return &message->target;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageSourceVendorIdGet (const gxPLMessage * message) {

  return message->source.vendor;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageSourceDeviceIdGet (const gxPLMessage * message) {

  return message->source.device;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageSourceInstanceIdGet (const gxPLMessage * message) {

  return message->source.instance;
}

// -----------------------------------------------------------------------------
int
gxPLMessageIsValid (const gxPLMessage * message) {

  return message->isvalid;
}

// -----------------------------------------------------------------------------
int
gxPLMessageIsError (const gxPLMessage * message) {

  return message->iserror;
}

// -----------------------------------------------------------------------------
gxPLMessageState
gxPLMessageStateGet (const gxPLMessage * message) {

  return message->state;
}

// -----------------------------------------------------------------------------
int
gxPLMessageFlagClear (gxPLMessage * message) {

  return message->flag = 0;
}

// -----------------------------------------------------------------------------
int
gxPLMessageIsBroadcast (const gxPLMessage * message) {

  return message->isbroadcast;
}

// -----------------------------------------------------------------------------
const gxPLSchema *
gxPLMessageSchemaGet (const gxPLMessage * message) {

  return &message->schema;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageSchemaClassGet (const gxPLMessage * message) {

  return message->schema.class;
}

// -----------------------------------------------------------------------------
const char *
gxPLMessageSchemaTypeGet (const gxPLMessage * message) {

  return message->schema.type;
}

// -----------------------------------------------------------------------------
int
gxPLMessageSchemaSet (gxPLMessage * message, const char * schema_class,
                      const char * schema_type) {

  return gxPLSchemaSet (&message->schema, schema_class, schema_type);
}

// -----------------------------------------------------------------------------
int
gxPLMessageSchemaCopy (gxPLMessage * message, const gxPLSchema * schema) {

  return gxPLSchemaCopy (&message->schema, schema);
}

// -----------------------------------------------------------------------------
int
gxPLMessageIsReceived (const gxPLMessage * message) {

  return message->isreceived;
}

// -----------------------------------------------------------------------------
int
gxPLMessageIsGrouped (const gxPLMessage * message) {

  return message->isgrouped;
}

// -----------------------------------------------------------------------------
xVector *
gxPLMessageBodyGet (gxPLMessage * message) {

  return &message->body;
}

// -----------------------------------------------------------------------------
const xVector *
gxPLMessageBodyGetConst (const gxPLMessage * message) {

  return &message->body;
}

// -----------------------------------------------------------------------------
int
gxPLMessageBodySize (const gxPLMessage * message) {

  return iVectorSize (&message->body);
}

// -----------------------------------------------------------------------------
int
gxPLMessageBodyClear (gxPLMessage * message) {

  return iVectorClear (&message->body);
}

// -----------------------------------------------------------------------------
int
gxPLMessageTypeSet (gxPLMessage * message, gxPLMessageType type) {

  message->type = type;
  return 0;
}

// -----------------------------------------------------------------------------
int
gxPLMessageBroadcastSet (gxPLMessage * message, bool isBroadcast) {

  message->isbroadcast = isBroadcast;
  return 0;
}

// -----------------------------------------------------------------------------
int
gxPLMessageReceivedSet (gxPLMessage * message, bool isReceived) {

  message->isreceived = isReceived;
  return 0;
}

// -----------------------------------------------------------------------------
int
gxPLMessageGroupedSet (gxPLMessage * message, bool isGrouped) {

  message->isgrouped = isGrouped;
  return 0;
}

// -----------------------------------------------------------------------------
int
gxPLMessageHopSet (gxPLMessage * message, int hop) {

  message->hop = hop;
  return 0;
}

// -----------------------------------------------------------------------------
int
gxPLMessageHopInc (gxPLMessage * message) {

  message->hop++;
  return 0;
}

// -----------------------------------------------------------------------------
int
gxPLMessageSourceIdSet (gxPLMessage * message, const gxPLId * id) {

  return gxPLIdCopy (&message->source, id);
}

// -----------------------------------------------------------------------------
int
gxPLMessageTargetIdSet (gxPLMessage * message, const gxPLId * id) {

  return gxPLIdCopy (&message->target, id);
}

// -----------------------------------------------------------------------------
int
gxPLMessageTargetSet (gxPLMessage * message, const char * vendor_id,
                      const char * device_id, const char * instance_id) {
  gxPLId id;
  strcpy (id.vendor, vendor_id);
  strcpy (id.device, device_id);
  strcpy (id.instance, instance_id);

  return gxPLMessageTargetIdSet (message, &id);
}

// -----------------------------------------------------------------------------
int
gxPLMessageFilterMatch (const gxPLMessage * message, const gxPLFilter * filter) {

  if ((filter->type != gxPLMessageAny) && (message->type != filter->type)) {

    return false;
  }
  if ( (strcmp (filter->source.vendor, "*") != 0) &&
       (strcmp (filter->source.vendor, message->source.vendor) != 0)) {

    return false;
  }
  if ( (strcmp (filter->source.device, "*") != 0) &&
       (strcmp (filter->source.device, message->source.device) != 0)) {

    return false;
  }
  if ( (strcmp (filter->source.instance, "*") != 0) &&
       (strcmp (filter->source.instance, message->source.instance) != 0)) {

    return false;
  }
  if ( (strcmp (filter->schema.class, "*") != 0) &&
       (strcmp (filter->schema.class, message->schema.class) != 0)) {

    return false;
  }
  if ( (strcmp (filter->schema.type, "*") != 0) &&
       (strcmp (filter->schema.type, message->schema.type) != 0)) {

    return false;
  }
  return true;
}

/* ========================================================================== */
