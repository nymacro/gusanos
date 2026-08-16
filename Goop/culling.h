#ifndef GUSANOS_CULLING_H
#define GUSANOS_CULLING_H

#include <stack>
#include <algorithm>
#include "util/rect.h"

// #define DEBUG_CULLER

#ifdef DEBUG_CULLER
#include <iostream>

using std::cout;
using std::endl;
#endif

template <class Derived>
struct Culler {
#define self (static_cast<Derived *>(this))

	Culler(Rect const &rect_) : rect(rect_) {}

	static int diffFix(int i) {
		return (i * 256);
	}

	template <int HDir>
	static int fix(int i) {
		return (i * 256) + (HDir < 0 ? 127 : 128);
	}

	static int integer(int f) {
		return f / 256;
	}

	static int slope(int dx, int dy) {
		return (dx * 256) / dy;
	}

	template <int HDir>
	bool scan(int &x, int limit, int y) {
		if (HDir == -1) {
			int m = std::max(limit - 1, rect.x1 - 1);

			for (; x > m; --x) {
				if (self->block(x, y))
					return true;
			}

			return false;
		} else if (HDir == 1) {
			int m = std::min(limit + 1, rect.x2 + 1);

			for (; x < m; ++x) {
				if (self->block(x, y))
					return true;
			}

			return false;
		} else
			assert(false);
	}

	template <int HDir>
	bool skip(int &x, int limit, int y) {
		if (HDir == -1) {
			int m = std::max(limit, rect.x1);

			if (x <= m)
				return true;

			if (!self->block(x, y))
				return false;

			for (--x; x > m; --x) {
				if (!self->block(x, y))
					return false;
			}

			return true;
		} else if (HDir == 1) {
			int m = std::min(limit, rect.x2);

			if (x >= m)
				return true;

			if (!self->block(x, y))
				return false;

			for (++x; x < m; ++x) {
				if (!self->block(x, y))
					return false;
			}

			return true;
		} else
			assert(false);
	}

	template <int VDir>
	bool checkY(int y) {
		if (VDir == -1) {
			if (y < rect.y1)
				return false;
		} else if (VDir == 1) {
			if (y > rect.y2)
				return false;
		}

		return true;
	}

	int extend(int f, int slope) // Returns the coverage of a slope
	{
		return integer(f + (slope / 2));
	}

	int getXOffset(int x) {
		return x - orgX;
	}

	int getYOffset(int y) {
		return abs(y - orgY);
	}

	void cullOmni(int x, int y) {
		if (!rect.isValid())
			return;

		if (x < rect.x1 || x > rect.x2 || y < rect.y1 || y > rect.y2)
			return;

		orgX = x;
		orgY = y;

		int xp = x;
		scan<1>(xp, rect.x2, y);
		int flslope = slope(getXOffset(xp), 1);
		int l = extend(fix<1>(xp), flslope);

		cullRows<-1, 1>(y - 1, l, fix<1>(xp - 1), diffFix(flslope), x + 2, fix<1>(x + 2), diffFix(1));
		cullRows<1, 1>(y + 1, l, fix<1>(xp - 1), diffFix(flslope), x + 2, fix<1>(x + 2), diffFix(1));

		int r = xp - 1;
		xp = x;
		scan<-1>(xp, rect.x1, y);
		flslope = slope(getXOffset(xp), 1);
		l = extend(fix<-1>(xp), flslope);

		cullRows<-1, -1>(y - 1, l, fix<-1>(xp + 1), diffFix(flslope), x - 2, fix<-1>(x - 2), diffFix(-1));
		self->line(y, xp + 1, r);
		cullRows<1, -1>(y + 1, l, fix<-1>(xp + 1), diffFix(flslope), x - 2, fix<-1>(x - 2), diffFix(-1));

		cullRowsStraight<-1>(y - 1, fix<1>(x + 1), diffFix(1), fix<1>(x - 1), diffFix(-1));
		cullRowsStraight<1>(y + 1, fix<1>(x + 1), diffFix(1), fix<1>(x - 1), diffFix(-1));
	}

	// Goes from r to l and generates new segments.
	template <int VDir, int HDir>
	void cullRows(int y, int l, int fl, int flslope, int r, int fr, int frslope) {
	tailcall:
		if (!checkY<VDir>(y))
			return;

		assert(getYOffset(y) != 0);

		int xp = r;
		while (scan<HDir>(xp, l, y)) {
			// * ***
			// x

			if (xp != r) // If we start in a blocked pixel, don't try to fill
			{
				if (HDir < 0)
					self->line(y, xp - HDir, r);
				else
					self->line(y, r, xp - HDir);

				if (checkY<VDir>(y + VDir)) {
					int t = xp - HDir;

					int nextflslope = slope(getXOffset(xp), getYOffset(y) + 1);
					int nextl = extend(fix<HDir>(t), nextflslope);
					int nextr = extend(fr, frslope);
					int nextfl = fix<HDir>(nextl);

					if (scan<HDir>(t, nextl, y + VDir)) {
						nextl = t - HDir;
						nextfl = fix<HDir>(nextl);
						nextflslope = slope(getXOffset(t), getYOffset(y) + 1);
					}

					cullRows<VDir, HDir>(y + VDir, nextl, nextfl, nextflslope, nextr, fr + frslope, frslope);
				}

				r = xp;
			}

			if (getYOffset(y) <= 1)
				return;

			if (skip<HDir>(r, l, y)) // Skip blocked pixels
				return;

			frslope = slope(getXOffset(r) - HDir, getYOffset(y) - 1);

			r = extend(fix<HDir>(r - HDir), frslope);

			fr = fix<HDir>(r);
			xp = r;
		}

		if (xp != r) {
			if (HDir < 0)
				self->line(y, xp - HDir, r);
			else
				self->line(y, r, xp - HDir);

			if (checkY<VDir>(y + VDir)) {
				int t = xp - HDir;
				l = extend(fl, flslope);
				r = extend(fr, frslope);

				if (scan<HDir>(t, l, y + VDir)) {
					l = t - HDir;
					fl = fix<HDir>(l);
					flslope = slope(getXOffset(t), getYOffset(y) + 1);
				}

				y += VDir;
				fl += flslope;
				fr += frslope;
				goto tailcall;
			}
		}
	}

	template <int VDir>
	void cullRowsStraight(int y, int fl, int flslope, int fr, int frslope) {
	tailcall:
		if (!checkY<VDir>(y))
			return;

		assert(getYOffset(y) != 0);

		int l = integer(fl);
		int r = integer(fr);

		// l is bound-tested, check r
		if (r < rect.x1)
			r = rect.x1;

		int xp = r;
		while (scan<1>(xp, l, y)) {
			// * ***
			// x

			if (xp != r) // If we start in a blocked pixel, don't try to fill
			{
				int t = xp - 1;

				self->line(y, r, t);

				if (checkY<VDir>(y + VDir)) {
					int nextflslope = slope(getXOffset(t), getYOffset(y));
					int nextfl = fix<1>(t) + nextflslope;
					int nextfr = fr + frslope;

					cullRowsStraight<VDir>(y + VDir, nextfl, nextflslope, nextfr, frslope);
				}

				r = xp;
			}

			if (skip<1>(r, l, y)) // Skip blocked pixels
				return;

			frslope = slope(getXOffset(r), getYOffset(y));

			fr = fix<1>(r);
			xp = r;
		}

		if (xp != r) {
			int t = xp - 1;
			self->line(y, r, t);

			if (checkY<VDir>(y + VDir)) {
				y += VDir;
				fl += flslope;
				fr += frslope;
				goto tailcall;
			}
		}
	}

	Rect rect;
	int orgX;
	int orgY;
};

#endif // GUSANOS_CULLING_H
