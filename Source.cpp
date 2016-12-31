#include "VapourSynth.h"
#include "VSHelper.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <malloc.h>

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
			auto TopFieldSum = 0., BottomFieldSum = 0., CurrentBaseColor = d->color[plane];
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
							TopFieldSum += srcp[y][x] - CurrentBaseColor;
					else
						for (auto x = 0; x < width; ++x)
							BottomFieldSum += srcp[y][x] - CurrentBaseColor;
			};
			auto FixFadesMode0 = [&]() {
				auto MeanSum = (TopFieldSum + BottomFieldSum) / 2.;
				for (auto y = 0; y < height; ++y)
					if (y % 2 == false)
						for (auto x = 0; x < width; ++x)
							dstp[y][x] = static_cast<float>((srcp[y][x] - CurrentBaseColor) * MeanSum / TopFieldSum + CurrentBaseColor);
					else
						for (auto x = 0; x < width; ++x)
							dstp[y][x] = static_cast<float>((srcp[y][x] - CurrentBaseColor) * MeanSum / BottomFieldSum + CurrentBaseColor);
			};
			auto FixFadesMode1 = [&]() {
				auto MinSum = std::min(TopFieldSum, BottomFieldSum);
				if (MinSum == TopFieldSum)
					for (auto y = 1; y < height; y += 2)
						for (auto x = 0; x < width; ++x) {
							dstp[y][x] = static_cast<float>((srcp[y][x] - CurrentBaseColor) * MinSum / BottomFieldSum + CurrentBaseColor);
							dstp[y - 1][x] = srcp[y - 1][x];
						}
				else
					for (auto y = 0; y < height; y += 2)
						for (auto x = 0; x < width; ++x) {
							dstp[y][x] = static_cast<float>((srcp[y][x] - CurrentBaseColor) * MinSum / TopFieldSum + CurrentBaseColor);
							dstp[y + 1][x] = srcp[y + 1][x];
						}
			};
			auto FixFadesMode2 = [&]() {
				auto MaxSum = std::max(TopFieldSum, BottomFieldSum);
				if (MaxSum == TopFieldSum)
					for (auto y = 1; y < height; y += 2)
						for (auto x = 0; x < width; ++x) {
							dstp[y][x] = static_cast<float>((srcp[y][x] - CurrentBaseColor) * MaxSum / BottomFieldSum + CurrentBaseColor);
							dstp[y - 1][x] = srcp[y - 1][x];
						}
				else
					for (auto y = 0; y < height; y += 2)
						for (auto x = 0; x < width; ++x) {
							dstp[y][x] = static_cast<float>((srcp[y][x] - CurrentBaseColor) * MaxSum / TopFieldSum + CurrentBaseColor);
							dstp[y + 1][x] = srcp[y + 1][x];
						}
			};
			auto GetNormalizedDifference = [&]() {
				auto FieldPixelCount = static_cast<int64_t>(width) * height / 2;
				return std::abs(TopFieldSum - BottomFieldSum) / FieldPixelCount;
			};
			auto CopyToDestinationFrame = [=]() {
				std::memcpy(dstp[0], srcp[0], width * height * sizeof(float));
			};
			Initialize();
			FixFadesPrepare();
			if (GetNormalizedDifference() < d->threshold)
				CopyToDestinationFrame();
			else
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
	auto d = new FixFadesData{ in, vsapi };
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
	d->threshold = vsapi->propGetFloat(in, "threshold", 0, &err);
	if (err)
		d->threshold = 0.002;
	if (d->threshold < 0.) {
		vsapi->setError(out, "FixFades: threshold must not be negative!");
		delete d;
		return;
	}
	auto InputColorChannelCount = vsapi->propNumElements(in, "color");
	if (InputColorChannelCount != -1) {
		if (d->vi->format->numPlanes != InputColorChannelCount) {
			vsapi->setError(out, "FixFades: Invalid color value for the input colorspace!");
			delete d;
			return;
		}
		for (auto i = 0; i < d->vi->format->numPlanes; ++i)
			d->color[i] = vsapi->propGetFloat(in, "color", i, nullptr);
	}
	vsapi->createFilter(in, out, "FixFades", fixfadesInit, fixfadesGetFrame, fixfadesFree, fmParallel, 0, d, core);
}

VS_EXTERNAL_API(auto) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("com.deinterlace.ftf", "ftf", "Fix Telecined Fades", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("FixFades", "clip:clip;mode:int:opt;threshold:float:opt;color:float[]:opt;", fixfadesCreate, nullptr, plugin);
}
