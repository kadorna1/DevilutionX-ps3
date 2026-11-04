#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <variant>

#include <Aulib/Decoder.h>

#ifdef USE_SDL3
#include <SDL3/SDL_mutex.h>
#else
#include <SDL_mutex.h>
#endif

#include "utils/sdl_compat.h"
#include "utils/sdl_mutex.h"

namespace devilution {

/**
 * @brief A Decoder interface implementations that simply has the samples pushed into it by the user.
 */
class PushAulibDecoder final : public ::Aulib::Decoder {
public:
	PushAulibDecoder(int numChannels, int sampleRate)
	    : numChannels_(numChannels)
	    , sampleRate_(sampleRate)
	    , max_queued_samples_(
	          static_cast<std::size_t>(numChannels > 0 ? numChannels : 1)
	          * static_cast<std::size_t>(sampleRate > 0 ? sampleRate : 22050) * 2)
	{
	}

	template <typename T>
	void PushSamples(const T *data, unsigned size) noexcept
	{
		if (size == 0)
			return;

		AudioQueueItem item { data, size };
		const auto lock = std::lock_guard(queue_mutex_);
		queue_.push(std::move(item));
		queued_samples_ += size;

		// Keep at most two seconds of interleaved sample values. When the
		// audio callback falls behind, discard the oldest audio so memory
		// stays bounded and playback can catch up instead of accumulating
		// an unbounded queue.
		while (queued_samples_ > max_queued_samples_ && !queue_.empty()) {
			AudioQueueItem &oldest = queue_.front();
			const std::size_t excess = queued_samples_ - max_queued_samples_;
			const unsigned discard = static_cast<unsigned>(
			    excess < oldest.len ? excess : oldest.len);
			oldest.pos += discard;
			oldest.len -= discard;
			queued_samples_ -= discard;
			dropped_samples_ += discard;
			if (oldest.len == 0)
				queue_.pop();
		}

		if (queued_samples_ > peak_queued_samples_)
			peak_queued_samples_ = queued_samples_;
	}

	[[nodiscard]] std::size_t QueueHighWaterSamples() noexcept
	{
		const auto lock = std::lock_guard(queue_mutex_);
		return peak_queued_samples_;
	}

	[[nodiscard]] std::size_t QueueDroppedSamples() noexcept
	{
		const auto lock = std::lock_guard(queue_mutex_);
		return dropped_samples_;
	}

	[[nodiscard]] std::size_t QueueLimitSamples() const noexcept
	{
		return max_queued_samples_;
	}

	void DiscardPendingSamples() noexcept;

	bool open(SDL_IOStream *rwops) override;

	[[nodiscard]] int getChannels() const override
	{
		return numChannels_;
	}

	[[nodiscard]] int getRate() const override
	{
		return sampleRate_;
	}

	bool rewind() override;
	[[nodiscard]] std::chrono::microseconds duration() const override;
	bool seekToTime(std::chrono::microseconds pos) override;

protected:
	int doDecoding(float buf[], int len, bool &callAgain) override;

private:
	struct AudioQueueItem {
		std::variant<
		    std::unique_ptr<int16_t[]>,
		    std::unique_ptr<uint8_t[]>>
		    data;
		unsigned len;
		unsigned pos;

		template <typename T>
		AudioQueueItem(const T *data, unsigned size)
		    : data { std::unique_ptr<T[]> { new T[size] } }
		    , len { size }
		    , pos { 0 }
		{
			std::memcpy(std::get<std::unique_ptr<T[]>>(this->data).get(), data, size * sizeof(T));
		}
	};

	const int numChannels_;
	const int sampleRate_;
	const std::size_t max_queued_samples_;

	// Requires holding the queue_mutex_.
	AudioQueueItem *Next();

	std::queue<AudioQueueItem> queue_;
	SdlMutex queue_mutex_;
	std::size_t queued_samples_ = 0;
	std::size_t peak_queued_samples_ = 0;
	std::size_t dropped_samples_ = 0;
};

} // namespace devilution
