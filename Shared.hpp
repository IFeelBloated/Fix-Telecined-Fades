#pragma once
#include "VapourSynth.h"
#include "VSHelper.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <immintrin.h>

#if defined(_MSC_VER)
#include <malloc.h>
#include "cpufeatures.hpp"
#elif defined(__GNUC__) || defined(__GNUG__) || defined(__clang__)
#include <alloca.h>
#include "cpufeatures_gnu.hpp"
#endif

struct FixFadesData final {
	const VSAPI *vsapi = nullptr;
	VSNodeRef *node = nullptr;
	const VSVideoInfo *vi = nullptr;
	bool illformed = false;
	int64_t mode = 0;
	double threshold = 0.;
	double color[3] = { 0., 0., 0. };
	bool optimization = false;
	FixFadesData(const VSMap *in, VSMap *out, const VSAPI *api) {
		vsapi = api;
		auto err = 0;
		auto InputColorChannelCount = vsapi->propNumElements(in, "color");
		node = vsapi->propGetNode(in, "clip", 0, nullptr);
		vi = vsapi->getVideoInfo(node);
		if (!isConstantFormat(vi) || vi->format->sampleType != stFloat || vi->format->bitsPerSample < 32) {
			vsapi->setError(out, "FixFades: input clip must be single precision fp, with constant dimensions.");
			illformed = true;
			return;
		}
		mode = vsapi->propGetInt(in, "mode", 0, &err);
		if (err)
			mode = 0;
		if (mode < 0 || mode > 2) {
			vsapi->setError(out, "FixFades: mode must be 0, 1, or 2!");
			illformed = true;
			return;
		}
		threshold = vsapi->propGetFloat(in, "threshold", 0, &err);
		if (err)
			threshold = 0.002;
		if (threshold < 0.) {
			vsapi->setError(out, "FixFades: threshold must not be negative!");
			illformed = true;
			return;
		}
		if (InputColorChannelCount != -1) {
			if (vi->format->numPlanes != InputColorChannelCount) {
				vsapi->setError(out, "FixFades: Invalid color value for the input colorspace!");
				illformed = true;
				return;
			}
			for (auto i = 0; i < vi->format->numPlanes; ++i)
				color[i] = vsapi->propGetFloat(in, "color", i, nullptr);
		}
		optimization = !!vsapi->propGetInt(in, "opt", 0, &err);
		if (err)
			optimization = true;
	}
	FixFadesData(FixFadesData &&) = delete;
	FixFadesData(const FixFadesData &) = delete;
	auto &operator=(FixFadesData &&) = delete;
	auto &operator=(const FixFadesData &) = delete;
	~FixFadesData() {
		vsapi->freeNode(node);
	}
};
