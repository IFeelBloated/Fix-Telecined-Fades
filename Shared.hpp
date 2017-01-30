#pragma once
#include "VapourSynth.h"
#include "VSHelper.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <immintrin.h>
#include <malloc.h>

#if defined(_MSC_VER)
#include "cpufeatures.hpp"
#elif defined(__GNUC__) || defined(__GNUG__) || defined(__clang__)
#include "cpufeatures_gnu.hpp"
#endif

struct FixFadesData final {
	const VSAPI *vsapi = nullptr;
	VSNodeRef *node = nullptr;
	const VSVideoInfo *vi = nullptr;
	int64_t mode = 0;
	double threshold = 0.;
	double color[3] = { 0., 0., 0. };
	FixFadesData(const VSMap *in, const VSAPI *api) {
		vsapi = api;
		node = vsapi->propGetNode(in, "clip", 0, nullptr);
		vi = vsapi->getVideoInfo(node);
	}
	FixFadesData(FixFadesData &&) = delete;
	FixFadesData(const FixFadesData &) = delete;
	auto &operator=(FixFadesData &&) = delete;
	auto &operator=(const FixFadesData &) = delete;
	~FixFadesData() {
		vsapi->freeNode(node);
	}
};
