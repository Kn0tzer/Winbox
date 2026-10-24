/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"
#include "x11.h"
#include "xserver.h"
#include <algorithm>
#include <cmath>

XDrawable::XDrawable(U32 width, U32 height, U32 depth, const VisualPtr& visual, bool isWindow) : id(XServer::getNextId()), isWindow(isWindow), depth(depth), visual(visual), w(width), h(height) {
	data = nullptr;
	setSize(width, height);
}

XDrawable::~XDrawable() {
	delete[] data;
}

U32 XDrawable::getImage(KThread* thread, S32 x, S32 y, U32 width, U32 height, U32 planeMask, U32 format, U32 redMask, U32 greenMask, U32 blueMask) {
	U32 image = thread->process->alloc(thread, sizeof(XImage));
	if (planeMask != AllPlanes) {
		kpanic_fmt("XDrawable::createXImage wasn't expecting planeMask = %x", planeMask);
	}
	if (format != ZPixmap) {
		kpanic_fmt("XDrawable::createXImage wasn't expecting format = %x", format);
	}
	U32 bytesPerLine = calculateBytesPerLine(width, visual->bits_per_rgb);
	U32 len = bytesPerLine * height;
	U32 data = thread->process->alloc(thread, len);

	U32 dst = data;
	U8* src = this->data + this->bytes_per_line * y + (visual->bits_per_rgb * x + 7) / 8;
	for (U32 y = 0; y < height; y++) {
		thread->memory->memcpy(dst, src, bytesPerLine);
		src += this->bytes_per_line;
		dst += bytesPerLine;
	}

	XImage::set(thread->memory, image, width, height, 0, format, data, 32, depth, bytesPerLine, visual->bits_per_rgb, redMask, greenMask, blueMask);
	return image;
}

U32 XDrawable::calculateBytesPerLine(U32 bitsPerPixel, U32 width) {
	U32 result = width * bitsPerPixel / 8;
	result = (result + 3) / 4 * 4;
	return result;
}

void XDrawable::lockData() {
	BOXEDWINE_MUTEX_LOCK(mutex);
}

void XDrawable::unlockData() {
	BOXEDWINE_MUTEX_UNLOCK(mutex);
}

void XDrawable::setSize(U32 width, U32 height) {
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(mutex);
	w = width;
	h = height;
	bytes_per_line = calculateBytesPerLine(visual?visual->bits_per_rgb:32, width);
	size = height * bytes_per_line;
	if (data) {
		delete[] data;
	}
	data = new U8[size];
	memset(data, 0, size);
}

int XDrawable::putImage(KThread* thread, const std::shared_ptr<XGC>& gc, XImage* image, S32 src_x, S32 src_y, S32 dest_x, S32 dest_y, U32 width, U32 height) {
	if (gc->values.function != GXcopy) {
		kwarn_fmt("XPixmap::putImage function not supported %d", gc->values.function);
	}
	return copyImageData(thread, gc, image->data, image->bytes_per_line, image->bits_per_pixel, src_x, src_y, dest_x, dest_y, width, height);
}

int XDrawable::copyImageData(KThread* thread, const std::shared_ptr<XGC>& gc, U32 data, U32 bytes_per_line, S32 bits_per_pixel, S32 src_x, S32 src_y, S32 dst_x, S32 dst_y, U32 width, U32 height) {
	if (bits_per_pixel != this->visual->bits_per_rgb) {
		return BadMatch;
	}
	if (gc && (gc->clip_rects.size() || gc->values.clip_mask || gc->values.clip_x_origin || gc->values.clip_y_origin)) {
		//klog("XDrawable::copyImageData clipping not implemented");
	}
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(mutex);
	U32 src = data + bytes_per_line * src_y + (bits_per_pixel * src_x + 7) / 8;
	U8* dst = this->data + this->bytes_per_line * dst_y + (bits_per_pixel * dst_x + 7) / 8;
	KMemory* memory = thread->memory;
	if (dst_x + width > w) {
		if ((S32)w < dst_x) {
			return Success;
		}
		width = w - dst_x;
	}
	if (dst_y + (S32)height > (S32)h) {
		if ((S32)h < dst_y) {
			return Success;
		}
		height = h - dst_y;
	}
	U32 copyPerLine = (bits_per_pixel * width + 7) / 8;

	if (!gc || gc->values.function == GXcopy) {
		for (U32 y = 0; y < height; y++) {
			if (!memory->canRead(src, copyPerLine)) {
				return BadValue;
			}
			memory->memcpy(dst, src, copyPerLine);
			src += bytes_per_line;
			dst += this->bytes_per_line;
		}
	} else if (gc->values.function == GXxor && bits_per_pixel == 32) {
		for (U32 y = 0; y < height; y++) {
			if (!memory->canRead(src, copyPerLine)) {
				return BadValue;
			}
			U32* dstPixel = (U32*)dst;
			for (U32 x = 0; x < width; x++) {
				dstPixel[x] ^= memory->readd(src + x * 4);
			}			
			src += bytes_per_line;
			dst += this->bytes_per_line;
		}
	}
	setDirty();
	return Success;
}

int XDrawable::copy(KThread* thread, const std::shared_ptr<XGC>& gc, const std::shared_ptr<XDrawable>& srcDrawable, S32 srcX, S32 srcY, U32 width, U32 height, S32 dstX, S32 dstY) {
	if (srcDrawable->visual->bits_per_rgb != this->visual->bits_per_rgb) {
		return BadMatch;
	}
	if (gc && (gc->clip_rects.size() || gc->values.clip_mask || gc->values.clip_x_origin || gc->values.clip_y_origin)) {
		//klog("XDrawable::copyImageData clipping not implemented");
	}
	U8* src = srcDrawable->data + srcDrawable->bytes_per_line * srcY + (srcDrawable->visual->bits_per_rgb * srcX + 7) / 8;
	U8* dst = this->data + this->bytes_per_line * dstY + (this->visual->bits_per_rgb * dstX + 7) / 8;

	if (dstX + width > w) {
		if ((S32)w < dstX) {
			return Success;
		}
		width = w - dstX;
	}
	if (dstY + (S32)height > (S32)h) {
		if ((S32)h < dstY) {
			return Success;
		}
		height = h - dstY;
	}
	U32 copyPerLine = (this->visual->bits_per_rgb * width + 7) / 8;

	for (U32 y = 0; y < height; y++) {
		memcpy(dst, src, copyPerLine);
		src += srcDrawable->bytes_per_line;
		dst += this->bytes_per_line;
	}
	setDirty();
	return Success;
}

// The 16 X11 GC logical operations on one 32-bit pixel: src is the GC foreground or tile pixel, dst is the drawable pixel.
static U32 combinePixel(S32 function, U32 src, U32 dst) {
	switch (function) {
	case GXclear: return 0;
	case GXand: return src & dst;
	case GXandReverse: return src & ~dst;
	case GXcopy: return src;
	case GXandInverted: return ~src & dst;
	case GXnoop: return dst;
	case GXxor: return src ^ dst;
	case GXor: return src | dst;
	case GXnor: return ~(src | dst);
	case GXequiv: return ~(src ^ dst);
	case GXinvert: return ~dst;
	case GXorReverse: return src | ~dst;
	case GXcopyInverted: return ~src;
	case GXorInverted: return ~src | dst;
	case GXnand: return ~(src & dst);
	case GXset: return 0xffffffff;
	default: return src;
	}
}

// With clip rectangles set, a pixel is drawn only when one rectangle contains it, offset by the clip origin.
// A clip_mask bitmap is not applied here.
static bool clipAllows(const XGC* gc, S32 x, S32 y) {
	if (gc->clip_rects.empty()) {
		return true;
	}
	for (const XRectangle& r : gc->clip_rects) {
		S32 left = r.x + gc->values.clip_x_origin;
		S32 top = r.y + gc->values.clip_y_origin;
		if (x >= left && x < left + (S32)r.width && y >= top && y < top + (S32)r.height) {
			return true;
		}
	}
	return false;
}

void XDrawable::plotPixel(const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 color) {
	if (x < 0 || y < 0 || x >= (S32)w || y >= (S32)h || !clipAllows(gc.get(), x, y)) {
		return;
	}
	U32* p = (U32*)(data + bytes_per_line * y + x * 4);
	*p = combinePixel(gc->values.function, color, *p);
}

int XDrawable::drawLine(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x1, S32 y1, S32 x2, S32 y2) {
	if (visual->bits_per_rgb != 32) {
		kwarn_fmt("XDrawable::drawLine depth %d not supported", visual->bits_per_rgb);
		return Success;
	}
	// Bresenham's line algorithm. Both endpoints are drawn, so a zero-length segment draws one pixel.
	const S32 dx = x2 > x1 ? x2 - x1 : x1 - x2;
	const S32 dy = y2 > y1 ? y1 - y2 : y2 - y1; // negated height
	const S32 sx = x1 < x2 ? 1 : -1;
	const S32 sy = y1 < y2 ? 1 : -1;
	S32 err = dx + dy;
	for (;;) {
		plotPixel(gc, x1, y1, gc->values.foreground);
		if (x1 == x2 && y1 == y2) {
			break;
		}
		const S32 e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x1 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y1 += sy;
		}
	}
	setDirty();
	return Success;
}

int XDrawable::fillRectangle(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height) {
	if (visual->bits_per_rgb != 32) {
		kwarn_fmt("XDrawable::fillRectangle only %d-bit not handled", visual->bits_per_rgb);
		return Success;
	}
	// FillTiled repeats the tile pixmap in both directions, anchored at the tile origin.
	XDrawablePtr tile;
	if (gc->values.fill_style == FillTiled && gc->values.tile) {
		tile = XServer::getServer()->getDrawable(gc->values.tile);
		if (tile && (tile->visual->bits_per_rgb != 32 || !tile->w || !tile->h)) {
			tile = nullptr;
		}
	}
	const S32 right = x + (S32)width;
	const S32 bottom = y + (S32)height;
	for (S32 dstY = y < 0 ? 0 : y; dstY < bottom && dstY < (S32)h; dstY++) {
		for (S32 dstX = x < 0 ? 0 : x; dstX < right && dstX < (S32)w; dstX++) {
			U32 color = gc->values.foreground;
			if (tile) {
				S32 tx = (dstX - gc->values.ts_x_origin) % (S32)tile->w;
				S32 ty = (dstY - gc->values.ts_y_origin) % (S32)tile->h;
				if (tx < 0) {
					tx += (S32)tile->w;
				}
				if (ty < 0) {
					ty += (S32)tile->h;
				}
				color = *(U32*)(tile->data + tile->bytes_per_line * ty + tx * 4);
			}
			plotPixel(gc, dstX, dstY, color);
		}
	}
	setDirty();
	return Success;
}

// Points are flat x,y pairs. With relative set, every point after the first is an offset from the point before it
static void toAbsolute(std::vector<S32>& xy, bool relative) {
	if (!relative) {
		return;
	}
	for (size_t i = 2; i + 1 < xy.size(); i += 2) {
		xy[i] += xy[i - 2];
		xy[i + 1] += xy[i - 1];
	}
}

int XDrawable::drawLines(KThread* thread, const std::shared_ptr<XGC>& gc, const std::vector<S32>& points, bool relative) {
	std::vector<S32> xy = points;
	toAbsolute(xy, relative);
	for (size_t i = 0; i + 3 < xy.size(); i += 2) {
		drawLine(thread, gc, xy[i], xy[i + 1], xy[i + 2], xy[i + 3]);
	}
	return Success;
}

// Rectangles are flat x,y,width,height quadruples
int XDrawable::fillRectangles(KThread* thread, const std::shared_ptr<XGC>& gc, const std::vector<S32>& rects) {
	for (size_t i = 0; i + 3 < rects.size(); i += 4) {
		fillRectangle(thread, gc, rects[i], rects[i + 1], (U32)rects[i + 2], (U32)rects[i + 3]);
	}
	return Success;
}

// Scan-converts the polygon one pixel row at a time. A pixel is inside when its centre is between two edge crossings
// that the GC fill rule (EvenOddRule or WindingRule) puts inside. Each inside run is a one-pixel-high fillRectangle, so the
// GC function, clip and tile apply the same way as for rectangles.
int XDrawable::fillPolygon(KThread* thread, const std::shared_ptr<XGC>& gc, const std::vector<S32>& points, bool relative) {
	if (visual->bits_per_rgb != 32) {
		kwarn_fmt("XDrawable::fillPolygon only %d-bit not handled", visual->bits_per_rgb);
		return Success;
	}
	std::vector<S32> xy = points;
	toAbsolute(xy, relative);
	const size_t vertices = xy.size() / 2;
	if (vertices < 3) {
		return Success; // fewer than three vertices enclose no area
	}
	S32 minY = xy[1];
	S32 maxY = xy[1];
	for (size_t k = 1; k < vertices; k++) {
		minY = std::min(minY, xy[2 * k + 1]);
		maxY = std::max(maxY, xy[2 * k + 1]);
	}
	minY = std::max(minY, 0);
	maxY = std::min(maxY, (S32)h - 1);
	const bool winding = gc->values.fill_rule == WindingRule;

	struct Crossing {
		double x;
		S32 dir;
	};
	std::vector<Crossing> crossings;
	for (S32 y = minY; y <= maxY; y++) {
		crossings.clear();
		const double sampleY = y + 0.5;
		for (size_t k = 0; k < vertices; k++) {
			const size_t j = (k + 1) % vertices;
			const S32 x0 = xy[2 * k], y0 = xy[2 * k + 1], x1 = xy[2 * j], y1 = xy[2 * j + 1];
			// an edge covers scan line y when y is in [min, max); horizontal edges never cross a scan line
			if (y0 == y1 || y < std::min(y0, y1) || y >= std::max(y0, y1)) {
				continue;
			}
			crossings.push_back({x0 + (sampleY - y0) * double(x1 - x0) / double(y1 - y0), y1 > y0 ? 1 : -1});
		}
		std::sort(crossings.begin(), crossings.end(), [](const Crossing& a, const Crossing& b) { return a.x < b.x; });
		S32 wind = 0;
		for (size_t c = 0; c + 1 < crossings.size(); c++) {
			wind += crossings[c].dir;
			// after crossing c there are c+1 crossings: even-odd is inside for an odd count, winding for a non-zero sum
			const bool inside = winding ? wind != 0 : (c % 2) == 0;
			if (!inside) {
				continue;
			}
			const S32 start = (S32)std::ceil(crossings[c].x - 0.5);
			const S32 end = (S32)std::ceil(crossings[c + 1].x - 0.5);
			if (end > start) {
				fillRectangle(thread, gc, start, y, (U32)(end - start), 1);
			}
		}
	}
	return Success;
}

int XDrawable::drawRectangle(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height) {
	// draw rectangle with no overlapping pixels
	drawLine(thread, gc, x, y, x + width, y); // top (includes left and right)
	drawLine(thread, gc, x + width, y + 1, x + width, y + height); // right	( includes bottom but not top)
	drawLine(thread, gc, x, y + 1, x, y + height); // left (includes bottom but not top)
	drawLine(thread, gc, x + 1, y + height, x + width - 1, y + height); // bottom (does not include left and right)
	return Success;
}
