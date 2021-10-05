/*
 * texture swizzling routines
 *
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2013 espes
 * Copyright (c) 2007-2010 The Nouveau Project.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "swizzle.h"
#include "state.h"


/* This should be pretty straightforward.
 * It creates a bit pattern like ..zyxzyxzyx from ..xxx, ..yyy and ..zzz
 * If there are no bits left from any component it will pack the other masks
 * more tighly (Example: zzxzxzyx = Fewer x than z and even fewer y)
 */
void swizzle_generate_masks(
  unsigned int width, unsigned int height, unsigned int depth,
  uint32_t* mask_x, uint32_t* mask_y, uint32_t* mask_z
) {
  uint32_t x = 0, y = 0, z = 0;
  uint32_t bit = 1;
  uint32_t mask_bit = 1;
  bool done;
  do {
    done = true;
    if (bit < width) { x |= mask_bit; mask_bit <<= 1; done = false; }
    if (bit < height) { y |= mask_bit; mask_bit <<= 1; done = false; }
    if (bit < depth) { z |= mask_bit; mask_bit <<= 1; done = false; }
    bit <<= 1;
  } while (!done);
  assert(x ^ y ^ z == (mask_bit - 1));
  *mask_x = x;
  *mask_y = y;
  *mask_z = z;
}

/* This fills a pattern with a value if your value has bits abcd and your
 * pattern is 11010100100 this will return: 0a0b0c00d00
 */
uint32_t swizzle_fill_pattern(uint32_t pattern, uint32_t value) {
  uint32_t result = 0;
  uint32_t bit = 1;
  while (value) {
    if (pattern & bit) {
      /* Copy bit to result */
      if (value & 1) {
        result |= bit;
      }
      value >>= 1;
    }
    bit <<= 1;
  }
  return result;
}

static inline unsigned int swizzle_get_offset(
  unsigned int x, unsigned int y, unsigned int z,
  uint32_t mask_x, uint32_t mask_y, uint32_t mask_z, unsigned int bytes_per_pixel
) {
  return bytes_per_pixel * 
    ( swizzle_fill_pattern(mask_x, x)
    | swizzle_fill_pattern(mask_y, y)
    | swizzle_fill_pattern(mask_z, z) );
}

static inline unsigned int swizzle_calculate_source_pitch(
    unsigned int width, unsigned int bytes_per_pixel
) {
  uint32_t num_components = 4;
  uint32_t component_size = 1;
  uint32_t pitch;
  uint32_t alignment_padding;
  uint32_t row_len = pbgl.pixelstore_unpack_state.row_length;
  if (!row_len) {
    row_len = width;
  }

  // TODO: Support components other than 4 (e.g., GL_RGB)
  pitch = row_len * num_components * component_size;
  alignment_padding = pitch % pbgl.pixelstore_unpack_state.alignment;
  if (alignment_padding) {
    pitch += pbgl.pixelstore_unpack_state.alignment - alignment_padding;
  }

  return pitch;
}

void swizzle_box(
  const uint8_t *src_buf,
  unsigned int width,
  unsigned int height,
  unsigned int depth,
  uint8_t *dst_buf,
  unsigned int row_pitch,
  unsigned int slice_pitch,
  unsigned int bytes_per_pixel
) {
  uint32_t mask_x, mask_y, mask_z;
  swizzle_generate_masks(width, height, depth, &mask_x, &mask_y, &mask_z);

  uint32_t src_pitch = swizzle_calculate_source_pitch(width, bytes_per_pixel);

  int x, y, z;
  for (z = 0; z < depth; z++) {
    for (y = 0; y < height; y++) {
      const uint8_t *src = src_buf + y * src_pitch;
      for (x = 0; x < width; x++, src += bytes_per_pixel) {
        unsigned int dst_offset = swizzle_get_offset(x, y, 0, mask_x, mask_y, 0, bytes_per_pixel);
        uint8_t *dst = dst_buf + dst_offset;
        memcpy(dst, src, bytes_per_pixel);
      }
    }
//    src_buf += slice_pitch;
  }
}

void swizzle_box_offset(
  const uint8_t *src_buf,
  unsigned int src_width,
  unsigned int src_height,
  unsigned int src_depth,
  uint8_t *dst_buf,
  unsigned int dst_xofs,
  unsigned int dst_yofs,
  unsigned int dst_zofs,
  unsigned int dst_width,
  unsigned int dst_height,
  unsigned int dst_depth,
  unsigned int row_pitch,
  unsigned int slice_pitch,
  unsigned int bytes_per_pixel
) {
  uint32_t mask_x, mask_y, mask_z;
  swizzle_generate_masks(dst_width, dst_height, dst_depth, &mask_x, &mask_y, &mask_z);

  int x, y, z;
  for (z = 0; z < src_depth; z++) {
    for (y = 0; y < src_height; y++) {
      for (x = 0; x < src_width; x++) {
        const uint8_t *src = src_buf + y * row_pitch + x * bytes_per_pixel;
        uint8_t *dst = dst_buf + swizzle_get_offset(x + dst_xofs, y + dst_yofs, 0, mask_x, mask_y, 0, bytes_per_pixel);
        memcpy(dst, src, bytes_per_pixel);
      }
    }
    src_buf += slice_pitch;
  }
}

void unswizzle_box(
  const uint8_t *src_buf,
  unsigned int width,
  unsigned int height,
  unsigned int depth,
  uint8_t *dst_buf,
  unsigned int row_pitch,
  unsigned int slice_pitch,
  unsigned int bytes_per_pixel
) {
  uint32_t mask_x, mask_y, mask_z;
  swizzle_generate_masks(width, height, depth, &mask_x, &mask_y, &mask_z);

  int x, y, z;
  for (z = 0; z < depth; z++) {
    for (y = 0; y < height; y++) {
      for (x = 0; x < width; x++) {
        const uint8_t *src = src_buf + swizzle_get_offset(x, y, z, mask_x, mask_y, mask_z, bytes_per_pixel);
        uint8_t *dst = dst_buf + y * row_pitch + x * bytes_per_pixel;
        memcpy(dst, src, bytes_per_pixel);
      }
    }
    dst_buf += slice_pitch;
  }
}

void unswizzle_rect(
  const uint8_t *src_buf,
  unsigned int width,
  unsigned int height,
  uint8_t *dst_buf,
  unsigned int pitch,
  unsigned int bytes_per_pixel
) {
  unswizzle_box(src_buf, width, height, 1, dst_buf, pitch, 0, bytes_per_pixel);
}

void swizzle_rect(
  const uint8_t *src_buf,
  unsigned int width,
  unsigned int height,
  uint8_t *dst_buf,
  unsigned int pitch,
  unsigned int bytes_per_pixel
) {
    swizzle_box(src_buf, width, height, 1, dst_buf, pitch, 0, bytes_per_pixel);
}

void swizzle_rect_offset(
  const uint8_t *src_buf,
  unsigned int src_width,
  unsigned int src_height,
  uint8_t *dst_buf,
  unsigned int dst_xofs,
  unsigned int dst_yofs,
  unsigned int dst_width,
  unsigned int dst_height,
  unsigned int pitch,
  unsigned int bytes_per_pixel
) {
    swizzle_box_offset(src_buf, src_width, src_height, 1, dst_buf, dst_xofs, dst_yofs, 0, dst_width, dst_height, 1, pitch, 0, bytes_per_pixel);
}
