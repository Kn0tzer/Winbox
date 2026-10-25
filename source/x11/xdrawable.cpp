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
	return copyImageData(thread, gc, image->data, image->bytes_per_line, image->bits_per_pixel, src_x, src_y, dest_x, dest_y, width, height);
}

int XDrawable::copyImageData(KThread* thread, const std::shared_ptr<XGC>& gc, U32 data, U32 bytes_per_line, S32 bits_per_pixel, S32 src_x, S32 src_y, S32 dst_x, S32 dst_y, U32 width, U32 height) {
	if (bits_per_pixel != this->visual->bits_per_rgb) {
		return BadMatch;
	}
	if (bits_per_pixel % 8) {
		kwarn_fmt("XDrawable::copyImageData %d-bit images not handled", bits_per_pixel);
		return Success;
	}
	// only the part of the image that lands inside the drawable is copied; a negative destination is cut off, not written
	const S32 x0 = std::max(dst_x, 0);
	const S32 y0 = std::max(dst_y, 0);
	const S32 x1 = std::min(dst_x + (S32)width, (S32)w);
	const S32 y1 = std::min(dst_y + (S32)height, (S32)h);
	if (x1 <= x0 || y1 <= y0) {
		return Success;
	}
	const bool fast = !gc || (gc->values.function == GXcopy && gc->clip_rects.empty() && !gc->values.clip_mask);
	BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(mutex);
	KMemory* memory = thread->memory;
	const U32 bytesPerPixel = bits_per_pixel / 8;
	const U32 count = (U32)(x1 - x0);
	for (S32 y = y0; y < y1; y++) {
		const U32 src = data + bytes_per_line * (U32)(src_y + y - dst_y) + bytesPerPixel * (U32)(src_x + x0 - dst_x);
		U8* dst = this->data + this->bytes_per_line * y + bytesPerPixel * x0;
		if (!memory->canRead(src, count * bytesPerPixel)) {
			return BadValue;
		}
		if (fast) {
			memory->memcpy(dst, src, count * bytesPerPixel);
		} else {
			for (U32 x = 0; x < count; x++) {
				const U32 guest = src + x * bytesPerPixel;
				const U32 pixel = bits_per_pixel == 8 ? memory->readb(guest) : bits_per_pixel == 16 ? memory->readw(guest) : memory->readd(guest);
				plotPixel(gc, x0 + (S32)x, y, pixel);
			}
		}
	}
	setDirty();
	return Success;
}

int XDrawable::copy(KThread* thread, const std::shared_ptr<XGC>& gc, const std::shared_ptr<XDrawable>& srcDrawable, S32 srcX, S32 srcY, U32 width, U32 height, S32 dstX, S32 dstY) {
	if (srcDrawable->visual->bits_per_rgb != this->visual->bits_per_rgb) {
		return BadMatch;
	}
	const S32 bits = this->visual->bits_per_rgb;
	if (bits % 8) {
		kwarn_fmt("XDrawable::copy %d-bit drawables not handled", bits);
		return Success;
	}
	// the destination pixels that have a source pixel inside the source drawable and a place inside this drawable
	const S32 x0 = std::max({dstX, 0, dstX - srcX});
	const S32 x1 = std::min({dstX + (S32)width, (S32)w, dstX - srcX + (S32)srcDrawable->w});
	const S32 y0 = std::max({dstY, 0, dstY - srcY});
	const S32 y1 = std::min({dstY + (S32)height, (S32)h, dstY - srcY + (S32)srcDrawable->h});
	if (x1 <= x0 || y1 <= y0) {
		return Success;
	}
	const bool fast = !gc || (gc->values.function == GXcopy && gc->clip_rects.empty() && !gc->values.clip_mask);
	const U32 bytesPerPixel = bits / 8;
	const S32 count = x1 - x0;
	std::vector<U32> run(count);
	// with one drawable as source and destination, a copy moved down must start from the bottom row so no row is read after it is written
	const bool backwards = srcDrawable.get() == this && dstY > srcY;
	for (S32 i = 0; i < y1 - y0; i++) {
		const S32 y = backwards ? y1 - 1 - i : y0 + i;
		const U8* srcRow = srcDrawable->data + srcDrawable->bytes_per_line * (y - dstY + srcY) + bytesPerPixel * (x0 - dstX + srcX);
		if (fast) {
			memmove(this->data + this->bytes_per_line * y + bytesPerPixel * x0, srcRow, bytesPerPixel * count);
			continue;
		}
		// the whole source run is read before any of it is written, so an overlapping copy in one drawable is correct
		for (S32 x = 0; x < count; x++) {
			run[x] = readStoredPixel(srcRow, srcDrawable->bytes_per_line, bits, x, 0);
		}
		for (S32 x = 0; x < count; x++) {
			plotPixel(gc, x0 + x, y, run[x]);
		}
	}
	setDirty();
	return Success;
}

// A source pixel with bit bitPlane set paints the GC foreground and a clear bit paints the GC background. Both go through the GC
// function and clip, as plotPixel does. The source bits are read before anything is written, so a copy within one drawable is correct.
int XDrawable::copyPlane(KThread* thread, const std::shared_ptr<XGC>& gc, const std::shared_ptr<XDrawable>& srcDrawable, S32 srcX, S32 srcY, U32 width, U32 height, S32 dstX, S32 dstY, U32 bitPlane) {
	const S32 x0 = std::max({dstX, 0, dstX - srcX});
	const S32 x1 = std::min({dstX + (S32)width, (S32)w, dstX - srcX + (S32)srcDrawable->w});
	const S32 y0 = std::max({dstY, 0, dstY - srcY});
	const S32 y1 = std::min({dstY + (S32)height, (S32)h, dstY - srcY + (S32)srcDrawable->h});
	if (x1 <= x0 || y1 <= y0) {
		return Success;
	}
	const S32 count = x1 - x0;
	std::vector<U8> set;
	set.reserve((size_t)count * (y1 - y0));
	for (S32 y = y0; y < y1; y++) {
		const U8* srcRow = srcDrawable->data + srcDrawable->bytes_per_line * (y - dstY + srcY);
		for (S32 x = x0; x < x1; x++) {
			const U32 pixel = readStoredPixel(srcRow, srcDrawable->bytes_per_line, srcDrawable->visual->bits_per_rgb, x - dstX + srcX, 0);
			set.push_back(bitPlane < 32 && ((pixel >> bitPlane) & 1));
		}
	}
	for (S32 y = y0; y < y1; y++) {
		for (S32 x = x0; x < x1; x++) {
			plotPixel(gc, x, y, set[(size_t)(y - y0) * count + (x - x0)] ? gc->values.foreground : gc->values.background);
		}
	}
	setDirty();
	return Success;
}


// Stored pixels are packed at the visual bits per pixel: 32 for the 24- and
// 32-bit visuals, 16 for the 16-bit visual and 8 for the 8-bit visual (see
// XServer::initVisuals and XDrawable::setSize, which sizes rows from the same
// value). Only byte-aligned formats are ever stored.
static U32 readStoredPixel(const U8* data, U32 bytesPerLine, S32 bitsPerPixel, S32 x, S32 y) {
	const U8* p = data + bytesPerLine * (U32)y + (U32)(bitsPerPixel / 8) * (U32)x;
	switch (bitsPerPixel) {
	case 8: return *p;
	case 16: { U16 v; memcpy(&v, p, sizeof(v)); return v; }
	default: { U32 v; memcpy(&v, p, sizeof(v)); return v; }
	}
}

static void writeStoredPixel(U8* data, U32 bytesPerLine, S32 bitsPerPixel, S32 x, S32 y, U32 value) {
	U8* p = data + bytesPerLine * (U32)y + (U32)(bitsPerPixel / 8) * (U32)x;
	switch (bitsPerPixel) {
	case 8: *p = (U8)value; break;
	case 16: { U16 v = (U16)value; memcpy(p, &v, sizeof(v)); break; }
	default: memcpy(p, &value, sizeof(value)); break;
	}
}

static U32 storedPixelMask(S32 bitsPerPixel) {
	return bitsPerPixel >= 32 ? 0xffffffffu : ((1u << (U32)bitsPerPixel) - 1u);
}

// The 16 X11 GC logical operations on one stored pixel: src is the GC foreground or tile pixel, dst is the drawable pixel.
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

static bool clipMaskAllows(const XDrawable* mask, S32 x, S32 y, S32 originX, S32 originY) {
	if (!mask) return true;
	const S32 mx = x - originX, my = y - originY;
	if (mx < 0 || my < 0 || mx >= (S32)mask->width() || my >= (S32)mask->height()) return false;
	const U32* pixels = (const U32*)mask->getData();
	return (pixels[(U32)my * (mask->getBytesPerLine() / 4) + (U32)mx] & 1) != 0;
}

// With clip rectangles set, a pixel is drawn only when one rectangle contains it, offset by the clip origin.
static bool clipAllows(const XGC* gc, S32 x, S32 y) {
	if (gc && gc->values.clip_mask) {
		XPixmapPtr mask = XServer::getServer()->getPixmap(gc->values.clip_mask);
		if (!mask || mask->getDepth() != 1 || !clipMaskAllows(mask.get(), x, y,
				gc->values.clip_x_origin, gc->values.clip_y_origin)) return false;
	}
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
	const S32 bits = visual->bits_per_rgb;
	const U32 dst = readStoredPixel(data, bytes_per_line, bits, x, y);
	writeStoredPixel(data, bytes_per_line, bits, x, y, combinePixel(gc->values.function, color, dst) & storedPixelMask(bits));
}

int XDrawable::drawLine(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x1, S32 y1, S32 x2, S32 y2) {
	// Bresenham's line algorithm. Both endpoints are drawn, so a zero-length segment draws one pixel.
	const S32 dx = x2 > x1 ? x2 - x1 : x1 - x2;
	const S32 dy = y2 > y1 ? y1 - y2 : y2 - y1; // negated height
	const S32 sx = x1 < x2 ? 1 : -1;
	const S32 sy = y1 < y2 ? 1 : -1;
	S32 err = dx + dy;
	U32 dashIndex = 0;
	U32 dashRemaining = 0;
	bool dashOn = true;
	const bool dashed = gc->values.line_style == LineOnOffDash || gc->values.line_style == LineDoubleDash;
	if (dashed) {
		const std::vector<U8>& pattern = gc->dash_list;
		U32 period = 0;
		for (U8 length : pattern) period += length;
		if (!period || pattern.empty()) {
			kwarn_fmt("XDrawable::drawLine invalid empty dash pattern; drawing solid");
		} else {
			U32 phase = ((U32)std::max<S32>(gc->values.dash_offset, 0)) % period;
			while (phase >= pattern[dashIndex]) {
				phase -= pattern[dashIndex];
				dashIndex = (dashIndex + 1) % (U32)pattern.size();
			}
			dashRemaining = pattern[dashIndex] - phase;
			dashOn = (dashIndex % 2) == 0;
		}
	}
	for (;;) {
		if (!dashed || !dashRemaining) {
			plotPixel(gc, x1, y1, gc->values.foreground);
		} else if (dashOn) {
			plotPixel(gc, x1, y1, gc->values.foreground);
		} else if (gc->values.line_style == LineDoubleDash) {
			plotPixel(gc, x1, y1, gc->values.background);
		}
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
		if (dashed && dashRemaining) {
			if (--dashRemaining == 0) {
				dashIndex = (dashIndex + 1) % (U32)gc->dash_list.size();
				dashRemaining = gc->dash_list[dashIndex];
				dashOn = (dashIndex % 2) == 0;
			}
		}
	}
	setDirty();
	return Success;
}

// The tile pixmap for a FillTiled GC, or null when the fill is solid or the tile cannot be used.
XDrawablePtr XDrawable::fillTile(const std::shared_ptr<XGC>& gc) {
	if (gc->values.fill_style != FillTiled || !gc->values.tile) {
		return nullptr;
	}
	XDrawablePtr tile = XServer::getServer()->getDrawable(gc->values.tile);
	if (tile && (!tile->w || !tile->h)) {
		return nullptr;
	}
	return tile;
}

// The colour of pixel (x, y) in a fill. The tile repeats in both directions, anchored at the tile origin.
U32 XDrawable::fillColor(const std::shared_ptr<XGC>& gc, const XDrawablePtr& tile, S32 x, S32 y) {
	if (!tile) {
		return gc->values.foreground;
	}
	S32 tx = (x - gc->values.ts_x_origin) % (S32)tile->w;
	S32 ty = (y - gc->values.ts_y_origin) % (S32)tile->h;
	if (tx < 0) {
		tx += (S32)tile->w;
	}
	if (ty < 0) {
		ty += (S32)tile->h;
	}
	return readStoredPixel(tile->data, tile->bytes_per_line, tile->visual->bits_per_rgb, tx, ty);
}

int XDrawable::fillRectangle(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height) {
	const XDrawablePtr tile = fillTile(gc);
	const S32 right = x + (S32)width;
	const S32 bottom = y + (S32)height;
	for (S32 dstY = y < 0 ? 0 : y; dstY < bottom && dstY < (S32)h; dstY++) {
		for (S32 dstX = x < 0 ? 0 : x; dstX < right && dstX < (S32)w; dstX++) {
			plotPixel(gc, dstX, dstY, fillColor(gc, tile, dstX, dstY));
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

namespace {
const double kPi = 3.14159265358979323846;

// The ellipse inscribed in a box, with the sweep of an arc. Angles are in 1/64 degree, counter-clockwise from 3 o'clock, and a
// negative sweep runs clockwise. A point's angle is taken after scaling the ellipse to a circle, which is where X puts the arc ends.
struct ArcShape {
	ArcShape(S32 x, S32 y, U32 width, U32 height, S32 angle1, S32 angle2)
		: cx(x + width / 2.0), cy(y + height / 2.0), rx(width / 2.0), ry(height / 2.0),
		  start(angle1 * kPi / (180.0 * 64.0)), sweep(angle2 * kPi / (180.0 * 64.0)),
		  full(angle2 >= 360 * 64 || angle2 <= -360 * 64) {}

	bool insideEllipse(double px, double py) const {
		const double u = (px - cx) / rx;
		const double v = (py - cy) / ry;
		return u * u + v * v <= 1.0;
	}

	// true when the point's angle from the centre is inside the sweep; the centre is on every pie slice
	bool inSweep(double px, double py) const {
		if (full || (px == cx && py == cy)) {
			return true;
		}
		const double twoPi = 2 * kPi;
		double t = std::fmod(std::atan2(-(py - cy) / ry, (px - cx) / rx) - start, twoPi);
		if (t < 0) {
			t += twoPi;
		}
		return sweep >= 0 ? t <= sweep : (t == 0 || t >= twoPi + sweep);
	}

	// the point on the ellipse at angle t, in pixel coordinates
	double pointX(double t) const { return cx + rx * std::cos(t); }
	double pointY(double t) const { return cy - ry * std::sin(t); }

	// signed side of the chord that joins the two ends of the arc
	double chordSide(double px, double py) const {
		const double x1 = pointX(start), y1 = pointY(start);
		const double x2 = pointX(start + sweep), y2 = pointY(start + sweep);
		return (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
	}

	double cx, cy, rx, ry;
	double start; // radians
	double sweep; // radians
	bool full;
};
} // namespace

// Fills the ellipse, a pie slice or a chord. The pixel centre decides, so a pixel is drawn when its centre is inside the shape.
int XDrawable::fillArc(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height, S32 angle1, S32 angle2) {
	if (!width || !height) {
		return Success;
	}
	const ArcShape arc(x, y, width, height, angle1, angle2);
	// a chord is the part of the ellipse on the same side of the chord line as the middle of the arc
	const bool chord = !arc.full && gc->values.arc_mode == ArcChord;
	const double middleSide = chord ? arc.chordSide(arc.pointX(arc.start + arc.sweep / 2), arc.pointY(arc.start + arc.sweep / 2)) : 0;
	const XDrawablePtr tile = fillTile(gc);
	const S32 right = x + (S32)width;
	const S32 bottom = y + (S32)height;
	for (S32 dstY = y < 0 ? 0 : y; dstY < bottom && dstY < (S32)h; dstY++) {
		for (S32 dstX = x < 0 ? 0 : x; dstX < right && dstX < (S32)w; dstX++) {
			const double px = dstX + 0.5;
			const double py = dstY + 0.5;
			if (!arc.insideEllipse(px, py)) {
				continue;
			}
			const bool inside = chord ? (middleSide != 0 && arc.chordSide(px, py) * middleSide >= 0) : arc.inSweep(px, py);
			if (inside) {
				plotPixel(gc, dstX, dstY, fillColor(gc, tile, dstX, dstY));
			}
		}
	}
	setDirty();
	return Success;
}

// Draws the outline of the ellipse or of the arc. The outline is always one pixel wide, so the GC line width is not used yet.
// Each column and each row contributes the pixel that the curve crosses there. Pixels are drawn once, so an XOR function does not cancel itself.
int XDrawable::drawArc(KThread* thread, const std::shared_ptr<XGC>& gc, S32 x, S32 y, U32 width, U32 height, S32 angle1, S32 angle2) {
	if (!width || !height) {
		return Success;
	}
	const ArcShape arc(x, y, width, height, angle1, angle2);
	std::vector<std::pair<S32, S32>> pixels;
	for (S32 px = x; px < x + (S32)width; px++) {
		const double sx = px + 0.5;
		const double u = (sx - arc.cx) / arc.rx;
		if (u * u > 1.0) {
			continue;
		}
		const double dy = arc.ry * std::sqrt(1.0 - u * u);
		for (const double sy : {arc.cy - dy, arc.cy + dy}) {
			if (arc.inSweep(sx, sy)) {
				pixels.push_back({px, (S32)std::floor(sy)});
			}
		}
	}
	for (S32 py = y; py < y + (S32)height; py++) {
		const double sy = py + 0.5;
		const double v = (sy - arc.cy) / arc.ry;
		if (v * v > 1.0) {
			continue;
		}
		const double dx = arc.rx * std::sqrt(1.0 - v * v);
		for (const double sx : {arc.cx - dx, arc.cx + dx}) {
			if (arc.inSweep(sx, sy)) {
				pixels.push_back({(S32)std::floor(sx), py});
			}
		}
	}
	std::sort(pixels.begin(), pixels.end());
	pixels.erase(std::unique(pixels.begin(), pixels.end()), pixels.end());
	for (const auto& p : pixels) {
		plotPixel(gc, p.first, p.second, gc->values.foreground);
	}
	setDirty();
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
