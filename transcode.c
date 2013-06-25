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

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "config.h"

#include "libav.h"

#include "upnpglobalvars.h"
#include "minidlnatypes.h"
#include "transcode.h"
#include "utils.h"
#include "log.h"

#define READ 0
#define WRITE 1

pid_t
popenvp(const char* file, char * const argv[], int *pipehandle)
{
	int fildes[2];
	pid_t pid;

	/* create a pipe */
	if(pipe(fildes)<0)
	{
		perror("popen2 pipe()");
		return -1;
	}

	/* Invoke processs */
	pid=fork();
	if (pid < 0)
	{
		perror("exec_transcode fork()");
		close(fildes[WRITE]);
		close(fildes[READ]);
		return pid;
	}
	/* child */
	if(pid == 0)
	{
		close(fildes[READ]);
		dup2(fildes[WRITE], WRITE);

		if (execvp(file, argv) < 0) {
			perror("exec_transcode execvp()");
			close(fildes[READ]);
			exit(1);
		}
	}
	else
	{
		close(fildes[WRITE]);
	}
	*pipehandle = fildes[READ];

	return pid;
}

/* NOTE: Partially based on Hiero's code */
pid_t
exec_transcode(char *transcoder, char *source_path, int offset, int end_offset, int *pipehandle)
{
	pid_t pid;
	char position[12], duration[12];
	static struct sigaction sa;
	char * args[5];

	sprintf(position, "%d.%d", offset/1000, offset%1000);
	sprintf(duration, "%d.%d", (end_offset - offset + 1)/1000,  (end_offset - offset + 1)%1000);

	args[0] = transcoder;
	args[1] = source_path;
	args[2] = position;
	args[3] = duration;
	args[4] = NULL;

	/* Invoke processs */
	pid = popenvp(args[0], args, pipehandle);
	if (pid < 0){
		perror("exec_transcode popen2vp()");
	}

	return pid;
}

pid_t
exec_transcode_img(char *transcoder, char *source_path, char *dest_path)
{
	pid_t pid;
	static struct sigaction sa;
	char * args[4];

	args[0] = transcoder;
	args[1] = source_path;
	args[2] = dest_path;
	args[3] = NULL;

	/* Invoke processs */
	pid=fork();
	if (pid < 0)
	{
		perror("exec_transcode_img fork()");
		return pid;
	}
	/* child */
	if(pid == 0)
	{
		if (execvp(args[0], args) < 0) {
			perror("exec_transcode_img execvp()");
			exit(1);
		}
	}

	return pid;
}

int
needs_transcode_image(const char* path, enum client_types client)
{
	/* this is a counter how many possible clients were checked, the checking ends when it's == 2 (default and specific client checked) */
	int checked_clients = 0;
	struct transcode_list_s *transcode_client_it = NULL;
	struct transcode_list_format_s *transcode_format_it = NULL;

	transcode_client_it = transcode_image;
	while ( transcode_client_it && checked_clients < 2 )
	{
		if ( transcode_client_it->type == 0 || transcode_client_it->type == client )
		{
			if ( transcode_client_it->type == 0)
				checked_clients++;
			if ( transcode_client_it->type == client )
				checked_clients++;

			transcode_format_it = transcode_client_it->formats;
			/* test for the reserved value "all" */
			if ( transcode_format_it && strcmp(transcode_format_it->value, "all") == 0 )
			{
				return 1;
			}
			while( transcode_format_it )
			{
				if ( ends_with(path, transcode_format_it->value) )
				{
					return 1;
				}
				transcode_format_it = transcode_format_it->next;
			}
		}
		transcode_client_it = transcode_client_it->next;
	}

	return 0;
}

int
needs_transcode_audio(const char* path, enum client_types client)
{
	int ret;
	AVFormatContext *ctx = NULL;
	struct AVCodecContext *ac = NULL;
	int i;
	int checked_clients = 0;
	struct transcode_list_s *transcode_client_it = NULL;
	struct transcode_list_format_s *transcode_format_it = NULL;
	struct AVCodec *codec = NULL;

	/* prepare ffmpeg */
	av_register_all();
	ret = lav_open(&ctx, path);
	if( ret != 0 )
	{
		char err[128];
		av_strerror(ret, err, sizeof(err));
		DPRINTF(E_WARN, L_METADATA, "Opening %s failed! [%s]\n", path, err);
		lav_close(ctx);
		return -1;
	}
	for( i=0; i<ctx->nb_streams; i++)
	{
		if( ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO )
		{
			ac = ctx->streams[i]->codec;
			break;
		}
	}
	if ( ac )
	{
		codec = avcodec_find_decoder(ac->codec_id);
		
		transcode_client_it = transcode_audio_codecs;
		while ( transcode_client_it && checked_clients < 2 )
		{
			if ( transcode_client_it->type == 0 || transcode_client_it->type == client )
			{
				if ( transcode_client_it->type == 0)
					checked_clients++;
				if ( transcode_client_it->type == client )
					checked_clients++;
				
				transcode_format_it = transcode_client_it->formats;
				/* test for the reserved value "all" */
				if ( transcode_format_it && strcmp(transcode_format_it->value, "all") == 0 )
				{
					lav_close(ctx);
					return 1;
				}

				while( transcode_format_it )
				{
					if ( strcmp(codec->name, transcode_format_it->value) == 0 )
					{
						lav_close(ctx);
						return 1;
					}
					transcode_format_it = transcode_format_it->next;
				}
			}
			transcode_client_it = transcode_client_it->next;
		}
	}

	lav_close(ctx);
	return 0;
}

int
needs_transcode_video(const char* path, enum client_types client)
{
	int ret;
	AVFormatContext *ctx = NULL;
	struct AVCodec *codec = NULL;
	int audio_stream = -1, video_stream = -1;
	struct AVCodecContext *ac = NULL;
	struct AVCodecContext *vc = NULL;
	int i;
	int checked_clients = 0;
	struct transcode_list_s *transcode_client_it = NULL;
	struct transcode_list_format_s *transcode_format_it = NULL;

	DPRINTF(E_WARN, L_METADATA, "Kontroluju video pro klienta: %d\n", client);

	/* prepare ffmpeg */
	av_register_all();
	ret = lav_open(&ctx, path);
	if( ret != 0 )
	{
		char err[128];
		av_strerror(ret, err, sizeof(err));
		DPRINTF(E_WARN, L_METADATA, "Opening %s failed! [%s]\n", path, err);
		return -1;
	}
	for( i=0; i<ctx->nb_streams; i++)
	{
		if( audio_stream == -1 &&
			ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO )
		{
			audio_stream = i;
			ac = ctx->streams[audio_stream]->codec;
			continue;
		}
		else if( video_stream == -1 &&
			ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO )
		{
			video_stream = i;
			vc = ctx->streams[video_stream]->codec;
			continue;
		}
	}
	if ( vc == NULL )
	{
		/* This must not be a video file. */
		DPRINTF(E_DEBUG, L_GENERAL, "File does not contain a video stream.\n");
		lav_close(ctx);
		return 0;
	}

	/* check if the file needs to be transcoded */

	/* check the container */
	transcode_client_it = transcode_video_containers;
	while ( transcode_client_it && checked_clients < 2 )
	{
		if ( transcode_client_it->type == 0 || transcode_client_it->type == client )
		{
			if ( transcode_client_it->type == 0)
				checked_clients++;
			if ( transcode_client_it->type == client )
				checked_clients++;

			transcode_format_it = transcode_client_it->formats;
			/* test for the reserved value "all" */
			if ( transcode_format_it && strcmp(transcode_format_it->value, "all") == 0 )
			{
				lav_close(ctx);
				return 1;
			}
			
			while( transcode_format_it )
			{
				if ( strcmp(ctx->iformat->name, transcode_format_it->value) == 0 )
				{
					lav_close(ctx);
					return 1;
				}
				transcode_format_it = transcode_format_it->next;
			}
		}
		transcode_client_it = transcode_client_it->next;
	}

	/* check the video codec */
	checked_clients = 0;
	transcode_client_it = transcode_video_codecs;
	while ( transcode_client_it && checked_clients < 2 )
	{
		if ( transcode_client_it->type == 0 || transcode_client_it->type == client )
		{
			if ( transcode_client_it->type == 0)
				checked_clients++;
			if ( transcode_client_it->type == client )
				checked_clients++;

			transcode_format_it = transcode_client_it->formats;
			/* test for the reserved value "all" */
			if ( transcode_format_it && strcmp(transcode_format_it->value, "all") == 0 )
			{
				lav_close(ctx);
				return 1;
			}
			
			codec = avcodec_find_decoder(vc->codec_id);
			while( transcode_format_it )
			{
				if ( strcmp(codec->name, transcode_format_it->value) == 0 )
				{
					lav_close(ctx);
					return 1;
				}
				transcode_format_it = transcode_format_it->next;
			}
		}
		transcode_client_it = transcode_client_it->next;
	}
	
	/* check the audio codec */
	checked_clients = 0;
	transcode_client_it = transcode_audio_codecs;
	while ( ac && transcode_client_it && checked_clients < 2 )
	{
		if ( transcode_client_it->type == 0 || transcode_client_it->type == client )
		{
			if ( transcode_client_it->type == 0)
				checked_clients++;
			if ( transcode_client_it->type == client )
				checked_clients++;
			
			transcode_format_it = transcode_client_it->formats;
			/* test for the reserved value "all" */
			if ( transcode_format_it && strcmp(transcode_format_it->value, "all") == 0 )
			{
				lav_close(ctx);
				return 1;
			}

			codec = avcodec_find_decoder(ac->codec_id);
			while( transcode_format_it )
			{
				if ( strcmp(codec->name, transcode_format_it->value) == 0 )
				{
					lav_close(ctx);
					return 1;
				}
				transcode_format_it = transcode_format_it->next;
			}
		}
		transcode_client_it = transcode_client_it->next;
	}

	lav_close(ctx);
	return 0;
}
