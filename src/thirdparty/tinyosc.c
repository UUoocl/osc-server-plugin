/**
 * Copyright (c) 2015-2018, Martin Roth (mhroth@gmail.com)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

#include "tinyosc.h"

#ifndef BUNDLE_ID
#define BUNDLE_ID 0x2362756e646c6500L // "#bundle"
#endif

// http://stackoverflow.com/questions/3022552/is-there-any-standard-htonll-in-c
#if defined(_WIN32) || defined(_WIN64)
// winsock2.h defines these
#else
#ifndef htonll
#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htonll(x) OSSwapHostToBigInt64(x)
#define ntohll(x) OSSwapBigToHostInt64(x)
#else
#include <endian.h>
#define htonll(x) htobe64(x)
#define ntohll(x) be64toh(x)
#endif
#endif
#endif

int tosc_parseMessage(tosc_message *o, char *buffer, const int len) {
  // NOTE(mhroth): if there's a comma in the address, that's weird
  int i = 0;
  while (buffer[i] != '\0' && i < len) ++i; // find the null-terminated address
  if (i == len) return -1;
  while (buffer[i] != ',' && i < len) ++i; // find the comma which starts the format string
  if (i >= len) return -1; // error while looking for format string
  // format string is null terminated
  o->format = buffer + i + 1; // format starts after comma

  while (i < len && buffer[i] != '\0') ++i;
  if (i == len) return -2; // format string not null terminated

  i = (i + 4) & ~0x3; // advance to the next multiple of 4 after trailing '\0'
  o->marker = buffer + i;

  o->buffer = buffer;
  o->len = len;

  return 0;
}

bool tosc_isBundle(const char *buffer) {
  return ((*(const int64_t *) buffer) == (int64_t) htonll(BUNDLE_ID));
}

void tosc_parseBundle(tosc_bundle *b, char *buffer, const int len) {
  b->buffer = (char *) buffer;
  b->marker = buffer + 16; // move past '#bundle ' and timetag fields
  b->bufLen = len;
  b->bundleLen = len;
}

uint64_t tosc_getTimetag(tosc_bundle *b) {
  return (uint64_t) ntohll(*((uint64_t *) (b->buffer+8)));
}

uint32_t tosc_getBundleLength(tosc_bundle *b) {
  return b->bundleLen;
}

bool tosc_getNextMessage(tosc_bundle *b, tosc_message *o) {
  if ((b->marker - b->buffer) >= (int32_t) b->bundleLen) return false;
  uint32_t len = (uint32_t) ntohl(*((uint32_t *) b->marker));
  tosc_parseMessage(o, b->marker+4, (int) len);
  b->marker += (4 + len); // move marker to next bundle element
  return true;
}

char *tosc_getAddress(tosc_message *o) {
  return o->buffer;
}

char *tosc_getFormat(tosc_message *o) {
  return o->format;
}

uint32_t tosc_getLength(tosc_message *o) {
  return o->len;
}

int32_t tosc_getNextInt32(tosc_message *o) {
  const int32_t i = (int32_t) ntohl(*((uint32_t *) o->marker));
  o->marker += 4;
  return i;
}

int64_t tosc_getNextInt64(tosc_message *o) {
  const int64_t i = (int64_t) ntohll(*((uint64_t *) o->marker));
  o->marker += 8;
  return i;
}

uint64_t tosc_getNextTimetag(tosc_message *o) {
  return (uint64_t) tosc_getNextInt64(o);
}

float tosc_getNextFloat(tosc_message *o) {
  const uint32_t i = (uint32_t) ntohl(*((uint32_t *) o->marker));
  o->marker += 4;
  return *((float *) (&i));
}

double tosc_getNextDouble(tosc_message *o) {
  const uint64_t i = (uint64_t) ntohll(*((uint64_t *) o->marker));
  o->marker += 8;
  return *((double *) (&i));
}

const char *tosc_getNextString(tosc_message *o) {
  int i = (int) strlen(o->marker);
  if (o->marker + i >= o->buffer + o->len) return NULL;
  const char *s = o->marker;
  i = (i + 4) & ~0x3; // advance to next multiple of 4 after trailing '\0'
  o->marker += i;
  return s;
}

void tosc_getNextBlob(tosc_message *o, const char **buffer, int *len) {
  int i = (int) ntohl(*((uint32_t *) o->marker)); // get the blob length
  if (o->marker + 4 + i <= o->buffer + o->len) {
    *len = i; // length of blob
    *buffer = o->marker + 4;
    i = (i + 4 + i + 3) & ~0x3;
    o->marker += i;
  } else {
    *len = 0;
    *buffer = NULL;
  }
}

unsigned char *tosc_getNextMidi(tosc_message *o) {
  unsigned char *m = (unsigned char *) o->marker;
  o->marker += 4;
  return m;
}

tosc_message *tosc_reset(tosc_message *o) {
  int i = 0;
  while (o->format[i] != '\0') ++i;
  i = (i + 4) & ~0x3; // advance to the next multiple of 4 after trailing '\0'
  o->marker = o->format + i - 1; // -1 to account for ',' format prefix
  return o;
}

void tosc_writeBundle(tosc_bundle *b, uint64_t timetag, char *buffer, const int len) {
  *((uint64_t *) buffer) = (uint64_t) htonll(BUNDLE_ID);
  *((uint64_t *) (buffer + 8)) = (uint64_t) htonll(timetag);

  b->buffer = buffer;
  b->marker = buffer + 16;
  b->bufLen = (uint32_t) len;
  b->bundleLen = 16;
}

static uint32_t tosc_vwrite(char *buffer, const int len,
    const char *address, const char *format, va_list ap) {
  memset(buffer, 0, len); // clear the buffer
  uint32_t i = (uint32_t) strlen(address);
  if (i >= (uint32_t) len) return (uint32_t) -1;
  strcpy(buffer, address);
  i = (i + 4) & ~0x3;
  buffer[i++] = ',';
  int s_len = (int) strlen(format);
  if ((int)(i + s_len) >= len) return (uint32_t) -2;
  strcpy(buffer+i, format);
  i = (i + 4 + s_len) & ~0x3;

  for (int j = 0; format[j] != '\0'; ++j) {
    switch (format[j]) {
      case 'b': {
        const uint32_t n = (uint32_t) va_arg(ap, int); // length of blob
        if (i + 4 + n > (uint32_t) len) return (uint32_t) -3;
        char *b = (char *) va_arg(ap, void *); // pointer to binary data
        *((uint32_t *) (buffer+i)) = (uint32_t) htonl(n); i += 4;
        memcpy(buffer+i, b, n);
        i = (i + n + 3) & ~0x3;
        break;
      }
      case 'f': {
        if (i + 4 > (uint32_t) len) return (uint32_t) -3;
        const float f = (float) va_arg(ap, double);
        *((uint32_t *) (buffer+i)) = (uint32_t) htonl(*((uint32_t *) &f));
        i += 4;
        break;
      }
      case 'd': {
        if (i + 8 > (uint32_t) len) return (uint32_t) -3;
        const double f = (double) va_arg(ap, double);
        *((uint64_t *) (buffer+i)) = (uint64_t) htonll(*((uint64_t *) &f));
        i += 8;
        break;
      }
      case 'i': {
        if (i + 4 > (uint32_t) len) return (uint32_t) -3;
        const uint32_t k = (uint32_t) va_arg(ap, int);
        *((uint32_t *) (buffer+i)) = (uint32_t) htonl(k);
        i += 4;
        break;
      }
      case 'm': {
        if (i + 4 > (uint32_t) len) return (uint32_t) -3;
        const unsigned char *const k = (unsigned char *) va_arg(ap, void *);
        memcpy(buffer+i, k, 4);
        i += 4;
        break;
      }
      case 't':
      case 'h': {
        if (i + 8 > (uint32_t) len) return (uint32_t) -3;
        const uint64_t k = (uint64_t) va_arg(ap, long long);
        *((uint64_t *) (buffer+i)) = (uint64_t) htonll(k);
        i += 8;
        break;
      }
      case 's': {
        const char *str = (const char *) va_arg(ap, void *);
        s_len = (int) strlen(str);
        if (i + s_len >= (uint32_t) len) return (uint32_t) -3;
        strcpy(buffer+i, str);
        i = (i + s_len + 4) & ~0x3;
        break;
      }
      case 'T': // true
      case 'F': // false
      case 'N': // nil
      case 'I': // infinitum
          break;
      default: return (uint32_t) -4; // unknown type
    }
  }

  return i; // return the total number of bytes written
}

uint32_t tosc_writeNextMessage(tosc_bundle *b,
    const char *address, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  if (b->bundleLen >= b->bufLen) return 0;
  const uint32_t i = tosc_vwrite(
      b->marker+4, (int) (b->bufLen-b->bundleLen-4), address, format, ap);
  va_end(ap);
  *((uint32_t *) b->marker) = (uint32_t) htonl(i); // write the length of the message
  b->marker += (4 + i);
  b->bundleLen += (4 + i);
  return i;
}

uint32_t tosc_vwriteMessage(char *buffer, const int len,
    const char *address, const char *format, va_list ap) {
  const uint32_t i = tosc_vwrite(buffer, len, address, format, ap);
  return i; // return the total number of bytes written
}

uint32_t tosc_writeMessage(char *buffer, const int len,
    const char *address, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  const uint32_t i = tosc_vwriteMessage(buffer, len, address, format, ap);
  va_end(ap);
  return i; // return the total number of bytes written
}
