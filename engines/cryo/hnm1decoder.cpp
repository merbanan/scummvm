/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "cryo/hnm1decoder.h"

#include "common/endian.h"
#include "common/stream.h"
#include "common/textconsole.h"

namespace Cryo {

// Decompress a stream using the same scheme as EdenGame::expandHSQ()
// (resource.cpp): a 16-bit, LSB-first bit-oriented LZ77 variant. Unlike
// expandHSQ(), this takes explicit input/output bounds instead of trusting
// the stream to self-terminate at the right place.
static bool decodeHSQFrame(const byte *src, uint32 srcSize, byte *dst, uint32 dstSize) {
	const byte *srcEnd = src + srcSize;
	byte *dstStart = dst;
	byte *dstEnd = dst + dstSize;
	uint16 queue = 0;

	auto getBit = [&]() -> int {
		int bit = queue & 1;
		queue >>= 1;
		if (!queue) {
			if (src + 2 > srcEnd)
				return -1;
			queue = src[0] | (src[1] << 8);
			src += 2;
			bit = queue & 1;
			queue = (queue >> 1) | 0x8000;
		}
		return bit;
	};

	for (;;) {
		int bit = getBit();
		if (bit < 0)
			return false;
		if (bit) {
			if (src >= srcEnd || dst >= dstEnd)
				return false;
			*dst++ = *src++;
		} else {
			int len = 0;
			int16 ofs;
			bit = getBit();
			if (bit < 0)
				return false;
			if (!bit) {
				for (int i = 0; i < 2; i++) {
					bit = getBit();
					if (bit < 0)
						return false;
					len = (len << 1) | bit;
				}
				if (src >= srcEnd)
					return false;
				ofs = 0xFF00 | *src++;
			} else {
				if (src + 2 > srcEnd)
					return false;
				uint16 tmp = src[0] | (src[1] << 8);
				src += 2;
				len = tmp & 7;
				ofs = (tmp >> 3) | 0xE000;
				if (!len) {
					if (src >= srcEnd)
						return false;
					len = *src++;
					if (!len)
						return true; // End of frame, regardless of exact byte count reached
				}
			}
			byte *from = dst + ofs;
			if (from < dstStart)
				return false;
			len += 2;
			for (; len > 0; len--) {
				if (dst >= dstEnd || from >= dstEnd)
					return false;
				*dst++ = *from++;
			}
		}
	}
}

HNM1Decoder::HNM1Decoder() : _stream(nullptr), _videoTrack(nullptr) {
}

HNM1Decoder::~HNM1Decoder() {
	close();
}

bool HNM1Decoder::loadStream(Common::SeekableReadStream *stream) {
	close();
	_stream = stream;

	uint32 streamSize = stream->size();
	if (streamSize < 5) {
		close();
		return false;
	}

	uint16 dataOffset = stream->readUint16LE();
	if (dataOffset < 3 || dataOffset >= streamSize) {
		close();
		return false;
	}

	// Palette chunk: same start/count/RGB triples encoding as HNM4's PL
	// chunk, terminated by start == 0xFF && count == 0xFF.
	byte rawPalette[256 * 3] = {};
	for (;;) {
		if (stream->pos() + 2 > dataOffset) {
			close();
			return false;
		}
		byte start = stream->readByte();
		byte count = stream->readByte();
		if (start == 0xFF && count == 0xFF)
			break;
		uint16 c = count ? count : 256;
		if (start + c > 256 || stream->pos() + c * 3 > dataOffset) {
			close();
			return false;
		}
		stream->read(rawPalette + start * 3, c * 3);
	}

	if (stream->readByte() != 0xFF) {
		close();
		return false;
	}

	// Cumulative offset table: N+1 entries, the last of which equals the
	// number of bytes remaining in the stream (i.e. it's an end marker
	// rather than an (N+1)th frame).
	Common::Array<uint32> frameOffsets;
	uint32 remaining = streamSize - dataOffset;
	for (;;) {
		if (stream->pos() + 4 > dataOffset) {
			close();
			return false;
		}
		uint32 v = stream->readUint32LE();
		frameOffsets.push_back(v);
		if (v == remaining)
			break;
		if (v > remaining || frameOffsets.size() > 4096) {
			close();
			return false;
		}
	}

	if ((uint32)stream->pos() != dataOffset) {
		close();
		return false;
	}

	// At least one real frame is needed (frameOffsets always has the end
	// marker, so an empty video has exactly one entry).
	if (frameOffsets.size() < 2) {
		close();
		return false;
	}

	// This same container also wraps unrelated resources (VOC audio with
	// lipsync data, playable through a completely different mechanism, not
	// implemented here). Both share the exact same palette/table framing,
	// so the only way to tell them apart cheaply is to check the first
	// frame's sub-header, which is only meaningful (and internally
	// consistent) for this video format: a fixed mode byte matching HNM4's
	// own unused intraframe header, and an inner size that is always the
	// outer size minus 6. Reject anything else here, at negligible cost,
	// rather than accepting it and burning through however many frames
	// (sometimes well over a thousand, e.g. GENERIQ.HNM) trying to decode
	// non-video data as video.
	uint32 firstFrameSize = frameOffsets[1] - frameOffsets[0];
	byte frameHeader[11];
	if (firstFrameSize < sizeof(frameHeader) ||
	        stream->read(frameHeader, sizeof(frameHeader)) != sizeof(frameHeader)) {
		close();
		return false;
	}
	uint16 selfSize = READ_LE_UINT16(frameHeader);
	byte mode = frameHeader[5];
	byte reserved = frameHeader[8];
	uint16 innerSize = READ_LE_UINT16(frameHeader + 9);
	if (selfSize != firstFrameSize || mode != 0xFE || reserved != 0 || innerSize != selfSize - 6) {
		close();
		return false;
	}

	_videoTrack = new HNM1VideoTrack(stream, dataOffset, frameOffsets, rawPalette);
	addTrack(_videoTrack);
	return true;
}

void HNM1Decoder::close() {
	VideoDecoder::close();
	_videoTrack = nullptr;

	delete _stream;
	_stream = nullptr;
}

HNM1Decoder::HNM1VideoTrack::HNM1VideoTrack(Common::SeekableReadStream *stream, uint32 dataOffset,
        const Common::Array<uint32> &frameOffsets, const byte *palette) :
	_stream(stream), _dataOffset(dataOffset), _frameOffsets(frameOffsets), _curFrame(-1),
	_palette(256), _dirtyPalette(true), _packedBuffer(nullptr), _packedBufferAlloc(0) {

	for (int i = 0; i < 256; i++) {
		byte r = palette[i * 3 + 0];
		byte g = palette[i * 3 + 1];
		byte b = palette[i * 3 + 2];
		_palette.set(i, r * 4, g * 4, b * 4);
	}

	_frameBuffer = new byte[kWidth * kHeight + kFrameBufferSlack];
	const Graphics::PixelFormat &f = Graphics::PixelFormat::createFormatCLUT8();
	_surface.init(kWidth, kHeight, kWidth * f.bytesPerPixel, nullptr, f);
}

HNM1Decoder::HNM1VideoTrack::~HNM1VideoTrack() {
	// Don't free _surface as we didn't use create(), just init()
	delete[] _frameBuffer;
	delete[] _packedBuffer;
}

bool HNM1Decoder::HNM1VideoTrack::endOfTrack() const {
	return _curFrame + 1 >= getFrameCount();
}

const Graphics::Surface *HNM1Decoder::HNM1VideoTrack::decodeNextFrame() {
	if (endOfTrack())
		return nullptr;

	_curFrame++;

	uint32 start = _dataOffset + _frameOffsets[_curFrame];
	uint32 end = _dataOffset + _frameOffsets[_curFrame + 1];
	if (end <= start + kFrameSubHeaderSize) {
		return &_surface;
	}
	uint32 frameSize = end - start;

	if (_packedBufferAlloc < frameSize) {
		delete[] _packedBuffer;
		_packedBuffer = new byte[frameSize];
		_packedBufferAlloc = frameSize;
	}
	_stream->seek(start, SEEK_SET);
	if (_stream->read(_packedBuffer, frameSize) != frameSize) {
		return &_surface;
	}

	// The first 12 bytes are a per-frame sub-header mirroring (and just as
	// unused as) HNM4's own width/height/mode intraframe header. Not every
	// resource using this container is actually this video format (some
	// wrap unrelated data, such as VOC audio); rather than trying to tell
	// those apart and rejecting them, just decode whatever comes out.
	decodeHSQFrame(_packedBuffer + kFrameSubHeaderSize, frameSize - kFrameSubHeaderSize,
	                _frameBuffer, kWidth * kHeight + kFrameBufferSlack);

	_surface.setPixels(_frameBuffer);
	return &_surface;
}

} // End of namespace Cryo
