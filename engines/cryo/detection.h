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

#ifndef CRYO_DETECTION_H
#define CRYO_DETECTION_H

namespace Cryo {

enum CryoGameFlags {
	/**
	 * Release which carries no game data, only movies sitting loose on the
	 * disc, and a list of them to walk through. The non-interactive demo is
	 * one: its DEMO.EXE plays a reel and its HNM.BAT hands the same files to
	 * an external viewer.
	 */
	GF_MOVIE_REEL = (1 << 0)
};

} // End of namespace Cryo

#endif
