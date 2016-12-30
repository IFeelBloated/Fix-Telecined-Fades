#include "VapourSynth.h"
#include "VSHelper.h"
#include <algorithm>
#include <malloc.h>

struct FixFadesData final {
	const VSAPI *vsapi = nullptr;
	VSNodeRef *node = nullptr;
	const VSVideoInfo *vi = nullptr;
	int64_t mode = 0;
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

auto VS_CC fixfadesInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	auto d = reinterpret_cast<FixFadesData *>(*instanceData);
	vsapi->setVideoInfo(d->vi, 1, node);
}

auto VS_CC fixfadesGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)->const VSFrameRef * {
	auto d = reinterpret_cast<FixFadesData *>(*instanceData);
	if (activationReason == arInitial)
		vsapi->requestFrameFilter(n, d->node, frameCtx);
	else if (activationReason == arAllFramesReady) {
		auto src = vsapi->getFrameFilter(n, d->node, frameCtx);
		auto fi = d->vi->format;
		auto height = vsapi->getFrameHeight(src, 0);
		auto width = vsapi->getFrameWidth(src, 0);
		auto dst = vsapi->newVideoFrame(fi, width, height, src, core);
		for (auto plane = 0; plane < fi->numPlanes; ++plane) {
			auto height = vsapi->getFrameHeight(src, plane);
			auto width = vsapi->getFrameWidth(src, plane);
			auto srcp = reinterpret_cast<const float **>(alloca(height * sizeof(void *)));
			auto dstp = reinterpret_cast<float **>(alloca(height * sizeof(void *)));
			auto TopFieldSum = 0., BottomFieldSum = 0.;
			auto Initialize = [=]() {
				auto src_stride = vsapi->getStride(src, plane) / sizeof(float);
				auto dst_stride = vsapi->getStride(dst, plane) / sizeof(float);
				for (auto i = 0; i < height; ++i) {
					srcp[i] = reinterpret_cast<const float *>(vsapi->getReadPtr(src, plane)) + i * src_stride;
					dstp[i] = reinterpret_cast<float *>(vsapi->getWritePtr(dst, plane)) + i * dst_stride;
				}
			};
			auto FixFadesPrepare = [&]() {
				for (auto y = 0; y < height; ++y)
					if (y % 2 == false)
						for (auto x = 0; x < width; ++x)
							TopFieldSum += srcp[y][x];
					else
						for (auto x = 0; x < width; ++x)
							BottomFieldSum += srcp[y][x];
			};
			auto FixFadesMode0 = [&]() {
				auto MeanSum = (TopFieldSum + BottomFieldSum) / 2.;
				for (auto y = 0; y < height; ++y)
					if (y % 2 == false)
						for (auto x = 0; x < width; ++x)
							dstp[y][x] = static_cast<float>(srcp[y][x] * MeanSum / TopFieldSum);
					else
						for (auto x = 0; x < width; ++x)
							dstp[y][x] = static_cast<float>(srcp[y][x] * MeanSum / BottomFieldSum);
			};
			auto FixFadesMode1 = [&]() {
				auto MinSum = std::min(TopFieldSum, BottomFieldSum);
				if (MinSum == TopFieldSum)
					for (auto y = 1; y < height; y += 2)
						for (auto x = 0; x < width; ++x) {
							dstp[y][x] = static_cast<float>(srcp[y][x] * MinSum / BottomFieldSum);
							dstp[y - 1][x] = srcp[y - 1][x];
						}
				else
					for (auto y = 0; y < height; y += 2)
						for (auto x = 0; x < width; ++x) {
							dstp[y][x] = static_cast<float>(srcp[y][x] * MinSum / TopFieldSum);
							dstp[y + 1][x] = srcp[y + 1][x];
						}
			};
			auto FixFadesMode2 = [&]() {
				auto MaxSum = std::max(TopFieldSum, BottomFieldSum);
				if (MaxSum == TopFieldSum)
					for (auto y = 1; y < height; y += 2)
						for (auto x = 0; x < width; ++x) {
							dstp[y][x] = static_cast<float>(srcp[y][x] * MaxSum / BottomFieldSum);
							dstp[y - 1][x] = srcp[y - 1][x];
						}
				else
					for (auto y = 0; y < height; y += 2)
						for (auto x = 0; x < width; ++x) {
							dstp[y][x] = static_cast<float>(srcp[y][x] * MaxSum / TopFieldSum);
							dstp[y + 1][x] = srcp[y + 1][x];
						}
			};
			Initialize();
			FixFadesPrepare();
			switch (d->mode) {
			case 0:
				FixFadesMode0();
				break;
			case 1:
				FixFadesMode1();
				break;
			case 2:
				FixFadesMode2();
				break;
			default:
				break;
			}
		}
		vsapi->freeFrame(src);
		return dst;
	}
	return nullptr;
}

auto VS_CC fixfadesFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	auto d = reinterpret_cast<FixFadesData *>(instanceData);
	delete d;
}

auto VS_CC fixfadesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	auto d = new FixFadesData(in, vsapi);
	auto err = 0;
	if (!isConstantFormat(d->vi) || d->vi->format->sampleType != stFloat || d->vi->format->bitsPerSample < 32) {
		vsapi->setError(out, "FixFades: input clip must be single precision fp, with constant dimensions.");
		delete d;
		return;
	}
	d->mode = vsapi->propGetInt(in, "mode", 0, &err);
	if (err)
		d->mode = 0;
	if (d->mode < 0 || d->mode > 2) {
		vsapi->setError(out, "FixFades: mode must be 0, 1, or 2!");
		delete d;
		return;
	}
	vsapi->createFilter(in, out, "FixFades", fixfadesInit, fixfadesGetFrame, fixfadesFree, fmParallel, 0, d, core);
}

VS_EXTERNAL_API(auto) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("com.deinterlace.ftf", "ftf", "Fix Telecined Fades", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("FixFades", "clip:clip;mode:int:opt;", fixfadesCreate, 0, plugin);
}
