#include <intrin.h>
#include <immintrin.h>
#include <cstdint>

constexpr auto operator""_i32(unsigned long long val) {
	return static_cast<int32_t>(val);
}

struct CPUFeatures final {
	bool can_run_vs = false;
	bool sse3 = false;
	bool ssse3 = false;
	bool sse4_1 = false;
	bool sse4_2 = false;
	bool fma3 = false;
	bool avx = false;
	bool avx2 = false;
	bool f16c = false;
	bool aes = false;
	bool movbe = false;
	bool popcnt = false;
	CPUFeatures() {
		decltype(0_i32) Registers[] = { 0_i32, 0_i32, 0_i32, 0_i32 };
		auto &eax = Registers[0];
		auto &ebx = Registers[1];
		auto &ecx = Registers[2];
		auto &edx = Registers[3];
		__cpuid(Registers, 1);
		can_run_vs = !!(edx & (1 << 26));
		sse3 = !!(ecx & 1);
		ssse3 = !!(ecx & (1 << 9));
		sse4_1 = !!(ecx & (1 << 19));
		sse4_2 = !!(ecx & (1 << 20));
		fma3 = !!(ecx & (1 << 12));
		f16c = !!(ecx & (1 << 29));
		aes = !!(ecx & (1 << 25));
		movbe = !!(ecx & (1 << 22));
		popcnt = !!(ecx & (1 << 23));
		if ((ecx & (1 << 27)) && (ecx & (1 << 28))) {
			eax = static_cast<decltype(eax + 0)>(_xgetbv(0) & 0x00000000FFFFFFFFull);
			avx = ((eax & 0x6) == 0x6);
			if (avx) {
				__cpuid(Registers, 7);
				avx2 = !!(ebx & (1 << 5));
			}
		}
	}
	CPUFeatures(const CPUFeatures &) = default;
	CPUFeatures(CPUFeatures &&) = default;
	auto operator=(const CPUFeatures &)->CPUFeatures & = default;
	auto operator=(CPUFeatures &&)->CPUFeatures & = default;
	~CPUFeatures() = default;
};
