/* MiniDLNA media server
 * Copyright (C) 2008-2012  Justin Maggard
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

#ifndef __DLNA_META_H__
#define __DLNA_META_H__

struct AVFormatContext;

struct dlna_meta_s
{
	char* mime;
	char* dlna_pn;
};

void
free_dlna_metadata(struct dlna_meta_s *m);

struct dlna_meta_s
get_dlna_metadata_image(int fd);

struct dlna_meta_s
get_dlna_metadata_image_res(char* format, int width, int height);

struct dlna_meta_s
get_dlna_metadata_audio(int fd);

struct dlna_meta_s
get_dlna_metadata_video(int fd);

struct dlna_meta_s
get_dlna_metadata_video_ctx(struct AVFormatContext *ctx, int audio_stream, int video_stream);

#endif /* __DLNA_META_H__ */
