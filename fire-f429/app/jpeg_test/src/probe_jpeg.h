/**
  * @file    probe_jpeg.h
  * @brief   JPEG-output probe for the fire-f429 OV5640 ("FD5640") module.
  *
  * Empirically determines whether the sensor can emit a valid JPEG stream on
  * the parallel 8-bit DVP bus. See probe_jpeg.c for the full test flow.
  */

#ifndef __PROBE_JPEG_H
#define __PROBE_JPEG_H

#include <stdint.h>

/* Public entry - runs the whole probe battery on the console. */
void ProbeJpeg_Run(void);

#endif /* __PROBE_JPEG_H */