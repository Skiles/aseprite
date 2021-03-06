/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2011  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include "filters/median_filter.h"

#include "base/memory.h"
#include "filters/filter_indexed_data.h"
#include "filters/filter_manager.h"
#include "filters/neighboring_pixels.h"
#include "filters/tiled_mode.h"
#include "raster/image.h"
#include "raster/palette.h"
#include "raster/rgbmap.h"

#include <algorithm>

MedianFilter::MedianFilter()
  : m_tiledMode(TILED_NONE)
  , m_width(0)
  , m_height(0)
  , m_ncolors(0)
  , m_channel(4)
{
}

void MedianFilter::setTiledMode(TiledMode tiled)
{
  m_tiledMode = tiled;
}

void MedianFilter::setSize(int width, int height)
{
  m_width = width;
  m_height = height;
  m_ncolors = width*height;

  for (int c = 0; c < 4; ++c)
    m_channel[c].resize(m_ncolors);
}

const char* MedianFilter::getName()
{
  return "Median Blur";
}

namespace {
  struct GetPixelsDelegateRgba
  {
    std::vector<std::vector<uint8_t> >& channel;
    int c;

    GetPixelsDelegateRgba(std::vector<std::vector<uint8_t> >& channel) : channel(channel) { }

    void reset() { c = 0; }

    void operator()(RgbTraits::pixel_t color)
    {
      channel[0][c] = _rgba_getr(color);
      channel[1][c] = _rgba_getg(color);
      channel[2][c] = _rgba_getb(color);
      channel[3][c] = _rgba_geta(color);
      c++;
    }
  };

  struct GetPixelsDelegateGrayscale
  {
    std::vector<std::vector<uint8_t> >& channel;
    int c;

    GetPixelsDelegateGrayscale(std::vector<std::vector<uint8_t> >& channel) : channel(channel) { }

    void reset() { c = 0; }

    void operator()(GrayscaleTraits::pixel_t color)
    {
      channel[0][c] = _graya_getv(color);
      channel[1][c] = _graya_geta(color);
      c++;
    }
  };

  struct GetPixelsDelegateIndexed
  {
    const Palette* pal;
    std::vector<std::vector<uint8_t> >& channel;
    Target target;
    int c;

    GetPixelsDelegateIndexed(const Palette* pal, std::vector<std::vector<uint8_t> >& channel, Target target)
      : pal(pal), channel(channel), target(target) { }

    void reset() { c = 0; }

    void operator()(IndexedTraits::pixel_t color)
    {
      if (target & TARGET_INDEX_CHANNEL) {
	channel[0][c] = color;
      }
      else {
	channel[0][c] = _rgba_getr(pal->getEntry(color));
	channel[1][c] = _rgba_getg(pal->getEntry(color));
	channel[2][c] = _rgba_getb(pal->getEntry(color));
      }
      c++;
    }
  };
};

void MedianFilter::applyToRgba(FilterManager* filterMgr)
{
  const Image* src = filterMgr->getSourceImage();
  uint32_t* dst_address = (uint32_t*)filterMgr->getDestinationAddress();
  Target target = filterMgr->getTarget();
  int color;
  int r, g, b, a;
  GetPixelsDelegateRgba delegate(m_channel);
  int x = filterMgr->getX();
  int x2 = x+filterMgr->getWidth();
  int y = filterMgr->getY();

  for (; x<x2; ++x) {
    // Avoid the non-selected region
    if (filterMgr->skipPixel()) {
      ++dst_address;
      continue;
    }

    delegate.reset();
    get_neighboring_pixels<RgbTraits>(src, x, y, m_width, m_height, m_width/2, m_height/2,
				      m_tiledMode, delegate);

    color = image_getpixel_fast<RgbTraits>(src, x, y);

    if (target & TARGET_RED_CHANNEL) {
      std::sort(m_channel[0].begin(), m_channel[0].end());
      r = m_channel[0][m_ncolors/2];
    }
    else
      r = _rgba_getr(color);

    if (target & TARGET_GREEN_CHANNEL) {
      std::sort(m_channel[1].begin(), m_channel[1].end());
      g = m_channel[1][m_ncolors/2];
    }
    else
      g = _rgba_getg(color);

    if (target & TARGET_BLUE_CHANNEL) {
      std::sort(m_channel[2].begin(), m_channel[2].end());
      b = m_channel[2][m_ncolors/2];
    }
    else
      b = _rgba_getb(color);

    if (target & TARGET_ALPHA_CHANNEL) {
      std::sort(m_channel[3].begin(), m_channel[3].end());
      a = m_channel[3][m_ncolors/2];
    }
    else
      a = _rgba_geta(color);

    *(dst_address++) = _rgba(r, g, b, a);
  }
}

void MedianFilter::applyToGrayscale(FilterManager* filterMgr)
{
  const Image* src = filterMgr->getSourceImage();
  uint16_t* dst_address = (uint16_t*)filterMgr->getDestinationAddress();
  Target target = filterMgr->getTarget();
  int color, k, a;
  GetPixelsDelegateGrayscale delegate(m_channel);
  int x = filterMgr->getX();
  int x2 = x+filterMgr->getWidth();
  int y = filterMgr->getY();

  for (; x<x2; ++x) {
    // Avoid the non-selected region
    if (filterMgr->skipPixel()) {
      ++dst_address;
      continue;
    }

    delegate.reset();
    get_neighboring_pixels<GrayscaleTraits>(src, x, y, m_width, m_height, m_width/2, m_height/2,
					    m_tiledMode, delegate);

    color = image_getpixel_fast<GrayscaleTraits>(src, x, y);

    if (target & TARGET_GRAY_CHANNEL) {
      std::sort(m_channel[0].begin(), m_channel[0].end());
      k = m_channel[0][m_ncolors/2];
    }
    else
      k = _graya_getv(color);

    if (target & TARGET_ALPHA_CHANNEL) {
      std::sort(m_channel[1].begin(), m_channel[1].end());
      a = m_channel[1][m_ncolors/2];
    }
    else
      a = _graya_geta(color);

    *(dst_address++) = _graya(k, a);
  }
}

void MedianFilter::applyToIndexed(FilterManager* filterMgr)
{
  const Image* src = filterMgr->getSourceImage();
  uint8_t* dst_address = (uint8_t*)filterMgr->getDestinationAddress();
  const Palette* pal = filterMgr->getIndexedData()->getPalette();
  const RgbMap* rgbmap = filterMgr->getIndexedData()->getRgbMap();
  Target target = filterMgr->getTarget();
  int color, r, g, b;
  GetPixelsDelegateIndexed delegate(pal, m_channel, target);
  int x = filterMgr->getX();
  int x2 = x+filterMgr->getWidth();
  int y = filterMgr->getY();

  for (; x<x2; ++x) {
    // Avoid the non-selected region
    if (filterMgr->skipPixel()) {
      ++dst_address;
      continue;
    }

    delegate.reset();
    get_neighboring_pixels<IndexedTraits>(src, x, y, m_width, m_height, m_width/2, m_height/2,
					  m_tiledMode, delegate);

    if (target & TARGET_INDEX_CHANNEL) {
      std::sort(m_channel[0].begin(), m_channel[0].end());
      *(dst_address++) = m_channel[0][m_ncolors/2];
    }
    else {
      color = image_getpixel_fast<IndexedTraits>(src, x, y);

      if (target & TARGET_RED_CHANNEL) {
	std::sort(m_channel[0].begin(), m_channel[0].end());
	r = m_channel[0][m_ncolors/2];
      }
      else
	r = _rgba_getr(pal->getEntry(color));

      if (target & TARGET_GREEN_CHANNEL) {
	std::sort(m_channel[1].begin(), m_channel[1].end());
	g = m_channel[1][m_ncolors/2];
      }
      else
	g = _rgba_getg(pal->getEntry(color));

      if (target & TARGET_BLUE_CHANNEL) {
	std::sort(m_channel[2].begin(), m_channel[2].end());
	b = m_channel[2][m_ncolors/2];
      }
      else
	b = _rgba_getb(pal->getEntry(color));

      *(dst_address++) = rgbmap->mapColor(r, g, b);
    }
  }
}
