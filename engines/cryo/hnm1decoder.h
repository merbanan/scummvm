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

#ifndef CRYO_HNM1DECODER_H
#define CRYO_HNM1DECODER_H

#include "common/array.h"
#include "graphics/palette.h"
#include "graphics/surface.h"
#include "video/video_decoder.h"

namespace Common {
class SeekableReadStream;
}

namespace Cryo {

/**
 * Decoder for an untagged, undocumented predecessor of Cryo's HNM4 video
 * format, found mixed in with regular HNM4 movies in Lost Eden's EDEN.DAT.
 *
 * Unlike HNM4, there is no magic tag, no superchunk framing, and no
 * interleaved audio. The stream layout is:
 *
 *   uint16 LE dataOffset   - offset where frame data begins
 *   palette chunk          - same start/count/RGB triples encoding as
 *                            HNM4's PL chunk, terminated by 0xFF,0xFF
 *   byte 0xFF              - marker
 *   uint32 LE offsets[N+1] - cumulative byte offsets of each of the N
 *                            frames, relative to dataOffset; offsets[N]
 *                            equals the number of bytes remaining in the
 *                            stream (i.e. it's an end marker, not a frame)
 *   N frames, each individually compressed to a fixed 320x160 image
 *
 * Each frame starts with a 12-byte sub-header that mirrors (and, like
 * HNM4's own intraframe header, is unused by) HNM4's width/height/mode
 * fields, followed by an LZ-compressed stream using the same scheme as
 * EdenGame::expandHSQ() (see resource.cpp) but with a 16-bit, LSB-first
 * bit reader rather than HNM4's own 32-bit, MSB-first one.
 */
class HNM1Decoder : public Video::VideoDecoder {
public:
	HNM1Decoder();
	~HNM1Decoder() override;

	bool loadStream(Common::SeekableReadStream *stream) override;
	void close() override;

private:
	class HNM1VideoTrack : public VideoTrack {
	public:
		HNM1VideoTrack(Common::SeekableReadStream *stream, uint32 dataOffset,
		               const Common::Array<uint32> &frameOffsets, const byte *palette);
		~HNM1VideoTrack() override;

		bool endOfTrack() const override;
		uint16 getWidth() const override { return kWidth; }
		uint16 getHeight() const override { return kHeight; }
		Graphics::PixelFormat getPixelFormat() const override { return _surface.format; }
		int getCurFrame() const override { return _curFrame; }
		int getFrameCount() const override { return _frameOffsets.size() - 1; }
		uint32 getNextFrameStartTime() const override { return (_curFrame + 1) * (uint32)kFrameDelayMs; }
		const Graphics::Surface *decodeNextFrame() override;
		const byte *getPalette() const override { _dirtyPalette = false; return _palette.data(); }
		bool hasDirtyPalette() const override { return _dirtyPalette; }

	private:
		enum {
			kWidth = 320,
			kHeight = 160,
			// No audio and no header field encodes a delay; this matches the
			// regular default used elsewhere for soundless HNM4 clips.
			kFrameDelayMs = 80,
			// Every frame observed so far starts with this fixed-size,
			// unused sub-header (see class comment).
			kFrameSubHeaderSize = 12,
			// The LZ stream's final copy can overshoot the logical end of
			// the frame by a handful of bytes (observed up to 4); allocate
			// some slack so that harmless trailing padding isn't mistaken
			// for a decode failure.
			kFrameBufferSlack = 16
		};

		Common::SeekableReadStream *_stream; // Not owned
		uint32 _dataOffset;
		Common::Array<uint32> _frameOffsets;
		int _curFrame;
		Graphics::Surface _surface;
		Graphics::Palette _palette;
		mutable bool _dirtyPalette;
		byte *_frameBuffer;
		byte *_packedBuffer;
		uint32 _packedBufferAlloc;
	};

	Common::SeekableReadStream *_stream;
	HNM1VideoTrack *_videoTrack;
};

} // End of namespace Cryo

#endif
