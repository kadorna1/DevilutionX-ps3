#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include "utils/file_util.h"

namespace devilution::md5 {

struct Context {
	std::array<std::uint32_t, 4> state {
		0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U
	};
	std::array<std::uint8_t, 64> buffer {};
	std::uint64_t totalBytes = 0;
	std::size_t bufferedBytes = 0;
};

inline constexpr std::array<std::uint32_t, 64> Constants {{
	0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
	0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
	0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
	0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
	0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
	0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
	0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
	0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
	0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
	0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
	0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
	0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
	0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
	0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
	0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
	0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
}};

inline constexpr std::array<unsigned, 64> Shifts {{
	7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
	5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
	4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
}};

inline std::uint32_t RotateLeft(std::uint32_t value, unsigned count)
{
	return (value << count) | (value >> (32 - count));
}

inline void Transform(Context &context, const std::uint8_t *block)
{
	std::array<std::uint32_t, 16> words {};
	for (std::size_t i = 0; i < words.size(); ++i) {
		const std::size_t offset = i * 4;
		words[i] = static_cast<std::uint32_t>(block[offset])
		    | (static_cast<std::uint32_t>(block[offset + 1]) << 8)
		    | (static_cast<std::uint32_t>(block[offset + 2]) << 16)
		    | (static_cast<std::uint32_t>(block[offset + 3]) << 24);
	}

	std::uint32_t a = context.state[0];
	std::uint32_t b = context.state[1];
	std::uint32_t c = context.state[2];
	std::uint32_t d = context.state[3];

	for (unsigned i = 0; i < 64; ++i) {
		std::uint32_t function;
		unsigned wordIndex;
		if (i < 16) {
			function = (b & c) | (~b & d);
			wordIndex = i;
		} else if (i < 32) {
			function = (d & b) | (~d & c);
			wordIndex = (5 * i + 1) % 16;
		} else if (i < 48) {
			function = b ^ c ^ d;
			wordIndex = (3 * i + 5) % 16;
		} else {
			function = c ^ (b | ~d);
			wordIndex = (7 * i) % 16;
		}

		const std::uint32_t next = b + RotateLeft(
		    a + function + Constants[i] + words[wordIndex], Shifts[i]);
		a = d;
		d = c;
		c = b;
		b = next;
	}

	context.state[0] += a;
	context.state[1] += b;
	context.state[2] += c;
	context.state[3] += d;
}

inline void Update(Context &context, const std::uint8_t *data, std::size_t length)
{
	context.totalBytes += length;
	while (length > 0) {
		const std::size_t amount = std::min(length, context.buffer.size() - context.bufferedBytes);
		std::memcpy(context.buffer.data() + context.bufferedBytes, data, amount);
		context.bufferedBytes += amount;
		data += amount;
		length -= amount;
		if (context.bufferedBytes == context.buffer.size()) {
			Transform(context, context.buffer.data());
			context.bufferedBytes = 0;
		}
	}
}

inline std::array<std::uint8_t, 16> Finalize(Context &context)
{
	const std::uint64_t bitLength = context.totalBytes * 8;
	context.buffer[context.bufferedBytes++] = 0x80;
	if (context.bufferedBytes > 56) {
		std::fill(context.buffer.begin() + context.bufferedBytes, context.buffer.end(), 0);
		Transform(context, context.buffer.data());
		context.bufferedBytes = 0;
	}
	std::fill(context.buffer.begin() + context.bufferedBytes, context.buffer.begin() + 56, 0);
	for (unsigned i = 0; i < 8; ++i)
		context.buffer[56 + i] = static_cast<std::uint8_t>(bitLength >> (8 * i));
	Transform(context, context.buffer.data());

	std::array<std::uint8_t, 16> digest {};
	for (std::size_t i = 0; i < context.state.size(); ++i) {
		for (unsigned byte = 0; byte < 4; ++byte)
			digest[i * 4 + byte] = static_cast<std::uint8_t>(context.state[i] >> (8 * byte));
	}
	return digest;
}

inline std::optional<std::string> CalculateFile(const char *path)
{
	FILE *file = OpenFile(path, "rb");
	if (file == nullptr)
		return std::nullopt;

	Context context;
	std::array<std::uint8_t, 64 * 1024> chunk {};
	bool readError = false;
	while (true) {
		const std::size_t bytesRead = std::fread(chunk.data(), 1, chunk.size(), file);
		if (bytesRead > 0)
			Update(context, chunk.data(), bytesRead);
		if (bytesRead < chunk.size()) {
			readError = std::ferror(file) != 0;
			break;
		}
	}
	std::fclose(file);
	if (readError)
		return std::nullopt;

	const std::array<std::uint8_t, 16> digest = Finalize(context);
	constexpr char Hex[] = "0123456789abcdef";
	std::string result;
	result.reserve(32);
	for (const std::uint8_t byte : digest) {
		result.push_back(Hex[byte >> 4]);
		result.push_back(Hex[byte & 0x0f]);
	}
	return result;
}

} // namespace devilution::md5
