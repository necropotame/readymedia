/* MiniDLNA media server
 * Copyright (C) 2012  Lukas Jirkovsky
 *
 * This file is part of MiniDLNA.
 *
 * MiniDLNA is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MiniDLNA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MiniDLNA. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TRANSCODE_H__
#define __TRANSCODE_H__

struct AVFormatContext;

pid_t
exec_transcode(char *transcoder, char *source_path, int offset, int end_offset, int *pipehandle);

pid_t
exec_transcode_img(char *transcoder, char *source_path, char *dest_path);

int
needs_transcode_image(const char* path);

int
needs_transcode_audio(const char* path);

int
needs_transcode_video(const char* path);

#endif /* __TRANSCODE_H__ */
