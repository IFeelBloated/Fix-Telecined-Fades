#include "VapourSynth.h"
#include "VSHelper.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <immintrin.h>
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
			constexpr auto BitMask = (0xFFFFFFFFFFFFFFFFull >> 3) << 3;
			auto WidthMod8 = width & BitMask;
			auto FieldPixelCount = static_cast<int64_t>(width) * height / 2;
			auto Initialize = [&]() {
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
				return std::abs(TopFieldSum - BottomFieldSum) / FieldPixelCount;
			};
			auto CopyToDestinationFrame = [&]() {
				std::memcpy(dstp[0], srcp[0], width * height * sizeof(float));
			};
			auto CopyLine = [&](auto y) {
				std::memcpy(dstp[y], srcp[y], width * sizeof(float));
			};
			auto ProcessLine_AVX_FMA = [&](auto y, auto FieldSum, auto ReferenceSum, auto &YMMFieldReference) {
				auto &YMM0 = _mm256_setzero_ps();
				auto &YMMCurrentBaseColor = _mm256_set1_ps(static_cast<float>(CurrentBaseColor));
				for (auto x = WidthMod8; x < width; ++x)
					dstp[y][x] = static_cast<float>((srcp[y][x] - CurrentBaseColor) * ReferenceSum / FieldSum + CurrentBaseColor);
				for (auto x = 0; x < WidthMod8; x += 8) {
					_mm256_store_ps(reinterpret_cast<float *>(&YMM0), _mm256_sub_ps(reinterpret_cast<const __m256 &>(srcp[y][x]), YMMCurrentBaseColor));
					_mm256_store_ps(&dstp[y][x], _mm256_fmadd_ps(YMM0, YMMFieldReference, YMMCurrentBaseColor));
				}
			};
			auto FixFadesPrepare_AVX = [&]() {
				auto &YMMTopField = _mm256_setzero_ps();
				auto &YMMBottomField = _mm256_setzero_ps();
				auto CalculateLine = [&](auto y, auto &FieldSum, auto &YMMField) {
					for (auto x = WidthMod8; x < width; ++x)
						FieldSum += srcp[y][x];
					for (auto x = 0; x < WidthMod8; x += 8)
						_mm256_store_ps(reinterpret_cast<float *>(&YMMField), _mm256_add_ps(reinterpret_cast<const __m256 &>(srcp[y][x]), YMMField));
				};
				auto YMMToFieldSum = [&]() {
					auto Offset = CurrentBaseColor * FieldPixelCount;
					for (auto i = 0; i < 8; ++i) {
						TopFieldSum += reinterpret_cast<float *>(&YMMTopField)[i];
						BottomFieldSum += reinterpret_cast<float *>(&YMMBottomField)[i];
					}
					TopFieldSum -= Offset;
					BottomFieldSum -= Offset;
				};
				for (auto y = 0; y < height; ++y)
					if (y % 2 == false)
						CalculateLine(y, TopFieldSum, YMMTopField);
					else
						CalculateLine(y, BottomFieldSum, YMMBottomField);
				YMMToFieldSum();
			};
			auto FixFadesMode0_AVX_FMA = [&]() {
				auto MeanSum = (TopFieldSum + BottomFieldSum) / 2.;
				auto &YMMBottomFieldReference = _mm256_set1_ps(static_cast<float>(MeanSum / BottomFieldSum));
				auto &YMMTopFieldReference = _mm256_set1_ps(static_cast<float>(MeanSum / TopFieldSum));
				for (auto y = 0; y < height; ++y)
					if (y % 2 == false)
						ProcessLine_AVX_FMA(y, TopFieldSum, MeanSum, YMMTopFieldReference);
					else
						ProcessLine_AVX_FMA(y, BottomFieldSum, MeanSum, YMMBottomFieldReference);
			};
			auto FixFadesMode1_AVX_FMA = [&]() {
				auto MinSum = std::min(TopFieldSum, BottomFieldSum);
				auto &YMMBottomFieldReference = _mm256_set1_ps(static_cast<float>(MinSum / BottomFieldSum));
				auto &YMMTopFieldReference = _mm256_set1_ps(static_cast<float>(MinSum / TopFieldSum));
				if (MinSum == TopFieldSum)
					for (auto y = 1; y < height; y += 2) {
						ProcessLine_AVX_FMA(y, BottomFieldSum, MinSum, YMMBottomFieldReference);
						CopyLine(y - 1);
					}
				else
					for (auto y = 0; y < height; y += 2) {
						ProcessLine_AVX_FMA(y, TopFieldSum, MinSum, YMMTopFieldReference);
						CopyLine(y + 1);
					}
			};
			auto FixFadesMode2_AVX_FMA = [&]() {
				auto MaxSum = std::max(TopFieldSum, BottomFieldSum);
				auto &YMMBottomFieldReference = _mm256_set1_ps(static_cast<float>(MaxSum / BottomFieldSum));
				auto &YMMTopFieldReference = _mm256_set1_ps(static_cast<float>(MaxSum / TopFieldSum));
				if (MaxSum == TopFieldSum)
					for (auto y = 1; y < height; y += 2) {
						ProcessLine_AVX_FMA(y, BottomFieldSum, MaxSum, YMMBottomFieldReference);
						CopyLine(y - 1);
					}
				else
					for (auto y = 0; y < height; y += 2) {
						ProcessLine_AVX_FMA(y, TopFieldSum, MaxSum, YMMTopFieldReference);
						CopyLine(y + 1);
					}
			};
			Initialize();
			FixFadesPrepare_AVX();
			if (GetNormalizedDifference() < d->threshold)
				CopyToDestinationFrame();
			else
				switch (d->mode) {
				case 0:
					FixFadesMode0_AVX_FMA();
					break;
				case 1:
					FixFadesMode1_AVX_FMA();
					break;
				case 2:
					FixFadesMode2_AVX_FMA();
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
