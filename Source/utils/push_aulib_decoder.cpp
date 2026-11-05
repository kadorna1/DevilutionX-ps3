#include "push_aulib_decoder.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <type_traits>
#include <variant>

#include <aulib.h>

#include "appfat.h"
#include "utils/log.hpp"

namespace devilution {

namespace {

float SampleToFloat(int16_t sample)
{
	constexpr float Factor = 1.0F / (std::numeric_limits<int16_t>::max() + 1);
	return sample * Factor;
}

float SampleToFloat(uint8_t sample)
{
	constexpr float Factor = 2.0F / std::numeric_limits<uint8_t>::max();
	return (sample * Factor) - 1;
}

template <typename T>
void ToFloats(const T *samples, float *out, unsigned count)
{
	std::transform(samples, samples + count, out, [](T sample) {
		return SampleToFloat(sample);
	});
}

} // namespace

void PushAulibDecoder::DiscardPendingSamples() noexcept
{
	const auto lock = std::lock_guard(queue_mutex_);
	queue_ = std::queue<AudioQueueItem>();
	queued_samples_ = 0;
}

bool PushAulibDecoder::open([[maybe_unused]] SDL_IOStream *rwops)
{
	assert(rwops == nullptr);
	return true;
}

bool PushAulibDecoder::rewind()
{
	return false;
}

std::chrono::microseconds PushAulibDecoder::duration() const
{
	return {};
}

bool PushAulibDecoder::seekToTime([[maybe_unused]] std::chrono::microseconds pos)
{
	return false;
}

int PushAulibDecoder::doDecoding(float buf[], int len, bool &callAgain)
{
	callAgain = false;
#ifdef __PS3__
	static std::atomic<uint32_t> callbackCount { 0 };
	const uint32_t callbackId = ++callbackCount;
	const bool traceCallback = callbackId <= 32 || callbackId % 128 == 0;
	if (traceCallback)
		LogInfo("PS3 FMV AUDIO CALLBACK: begin id={} requested_floats={}", callbackId, len);
#endif

	constexpr auto WriteFloats = [](PushAulibDecoder::AudioQueueItem &item, float *out, unsigned count) {
		std::visit([&](const auto &samples) { ToFloats(&samples[item.pos], out, count); }, item.data);
	};

	unsigned remaining = len;
	{
#ifdef __PS3__
		if (traceCallback)
			LogInfo("PS3 FMV AUDIO CALLBACK: queue lock begin id={}", callbackId);
#endif
		const auto lock = std::lock_guard(queue_mutex_);
#ifdef __PS3__
		if (traceCallback)
			LogInfo("PS3 FMV AUDIO CALLBACK: queue lock acquired id={} queued_samples={}", callbackId, queued_samples_);
#endif
		AudioQueueItem *item;
		while ((item = Next()) != nullptr) {
			if (remaining <= item->len) {
				WriteFloats(*item, buf, remaining);
				item->pos += remaining;
				item->len -= remaining;
				queued_samples_ -= remaining;
				if (item->len == 0)
					queue_.pop();
#ifdef __PS3__
				if (traceCallback)
					LogInfo("PS3 FMV AUDIO CALLBACK: complete id={} remaining_queue={}", callbackId, queued_samples_);
#endif
				return len;
			}

			WriteFloats(*item, buf, item->len);
			buf += item->len;
			remaining -= static_cast<int>(item->len);
			queued_samples_ -= item->len;
			queue_.pop();
		}
	}
	std::memset(buf, 0, remaining * sizeof(buf[0]));
#ifdef __PS3__
	if (traceCallback)
		LogInfo("PS3 FMV AUDIO CALLBACK: complete id={} silent_floats={} remaining_queue={}",
		    callbackId, remaining, queued_samples_);
#endif
	return len;
}

PushAulibDecoder::AudioQueueItem *PushAulibDecoder::Next()
{
	while (!queue_.empty() && queue_.front().len == 0)
		queue_.pop();
	if (queue_.empty())
		return nullptr;
	return &queue_.front();
}

} // namespace devilution
