#include "Shared.hpp"

auto VS_CC fixfadesGetFrame_AVX_FMA(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)->const VSFrameRef * {
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
			auto GetNormalizedDifference = [&]() {
				return std::abs(TopFieldSum - BottomFieldSum) / FieldPixelCount;
			};
			auto CopyToDestinationFrame = [&]() {
				std::memcpy(dstp[0], srcp[0], width * height * sizeof(float));
			};
			auto CopyLine = [&](auto y) {
				std::memcpy(dstp[y], srcp[y], width * sizeof(float));
			};
			auto ProcessLine = [&](auto y, auto FieldSum, auto ReferenceSum) {
				auto &&YMMCurrentBaseColor = _mm256_set1_ps(static_cast<float>(CurrentBaseColor));
				auto &&YMMFieldReference = _mm256_set1_ps(static_cast<float>(ReferenceSum / FieldSum));
				for (auto x = WidthMod8; x < width; ++x)
					dstp[y][x] = static_cast<float>((srcp[y][x] - CurrentBaseColor) * ReferenceSum / FieldSum + CurrentBaseColor);
				for (auto x = 0; x < WidthMod8; x += 8) {
					auto &&YMM0 = _mm256_sub_ps(reinterpret_cast<const __m256 &>(srcp[y][x]), YMMCurrentBaseColor);
					_mm256_store_ps(&dstp[y][x], _mm256_fmadd_ps(YMM0, YMMFieldReference, YMMCurrentBaseColor));
				}
			};
			auto FixFadesPrepare = [&]() {
				auto &&YMMTopField = _mm256_setzero_ps();
				auto &&YMMBottomField = _mm256_setzero_ps();
				auto CalculateLine = [&](auto y, auto &FieldSum, auto &YMMField) {
					for (auto x = WidthMod8; x < width; ++x)
						FieldSum += srcp[y][x];
					for (auto x = 0; x < WidthMod8; x += 8)
						YMMField = _mm256_add_ps(reinterpret_cast<const __m256 &>(srcp[y][x]), YMMField);
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
			auto FixFadesMode0 = [&]() {
				auto MeanSum = (TopFieldSum + BottomFieldSum) / 2.;
				for (auto y = 0; y < height; ++y)
					if (y % 2 == false)
						ProcessLine(y, TopFieldSum, MeanSum);
					else
						ProcessLine(y, BottomFieldSum, MeanSum);
			};
			auto FixFadesMode1 = [&]() {
				auto MinSum = std::min(TopFieldSum, BottomFieldSum);
				if (MinSum == TopFieldSum)
					for (auto y = 1; y < height; y += 2) {
						ProcessLine(y, BottomFieldSum, MinSum);
						CopyLine(y - 1);
					}
				else
					for (auto y = 0; y < height; y += 2) {
						ProcessLine(y, TopFieldSum, MinSum);
						CopyLine(y + 1);
					}
			};
			auto FixFadesMode2 = [&]() {
				auto MaxSum = std::max(TopFieldSum, BottomFieldSum);
				if (MaxSum == TopFieldSum)
					for (auto y = 1; y < height; y += 2) {
						ProcessLine(y, BottomFieldSum, MaxSum);
						CopyLine(y - 1);
					}
				else
					for (auto y = 0; y < height; y += 2) {
						ProcessLine(y, TopFieldSum, MaxSum);
						CopyLine(y + 1);
					}
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
