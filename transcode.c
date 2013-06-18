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
needs_transcode_image(const char* path)
{
	struct transcode_list_s *transcode_it = NULL;
	transcode_it = transcode_image;
	/* test for the reserved value "all" */
	if ( transcode_it && strcmp(transcode_it->value, "all") == 0 )
	{
		return 1;
	}
	while( transcode_it )
	{
		if ( ends_with(path, transcode_it->value) )
		{
			return 1;
		}
		transcode_it = transcode_it->next;
	}
	return 0;
}

int
needs_transcode_audio(const char* path)
{
	int ret;
	AVFormatContext *ctx = NULL;
	struct AVCodecContext *ac = NULL;
	int i;
	struct transcode_list_s *transcode_it = transcode_audio_codecs;
	/* test for the reserved value "all" */
	if ( transcode_it && strcmp(transcode_it->value, "all") == 0 )
	{
		return 1;
	}

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

	while( ac && transcode_it )
	{
		struct AVCodec * codec = avcodec_find_decoder(ac->codec_id);
		if ( strcmp(codec->name, transcode_it->value) == 0 )
		{
			lav_close(ctx);
			return 1;
		}
		transcode_it = transcode_it->next;
	}
	
	lav_close(ctx);
	return 0;
}

int
needs_transcode_video(const char* path)
{
	int ret;
	AVFormatContext *ctx = NULL;
	int audio_stream = -1, video_stream = -1;
	struct AVCodecContext *ac = NULL;
	struct AVCodecContext *vc = NULL;
	int i;
	struct transcode_list_s *transcode_it = transcode_video_containers;

	/* test for the reserved value "all" */
	if ( transcode_it && strcmp(transcode_it->value, "all") == 0 )
	{
		return 1;
	}

	DPRINTF(E_WARN, L_METADATA, "Opening %s\n", path);

	ret = lav_open(&ctx, path);
	if( ret != 0 )
	{
		char err[128];
		av_strerror(ret, err, sizeof(err));
		DPRINTF(E_WARN, L_METADATA, "Opening %s failed! [%s]\n", path, err);
		return -1;
	}
	DPRINTF(E_WARN, L_METADATA, "DONE\n");
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

	while( transcode_it )
	{
		if ( strcmp(ctx->iformat->name, transcode_it->value) == 0 )
		{
			lav_close(ctx);
			return 1;
		}
		transcode_it = transcode_it->next;
	}
	transcode_it = transcode_video_codecs;
	while( transcode_it )
	{
		struct AVCodec * codec = avcodec_find_decoder(vc->codec_id);
		if ( strcmp(codec->name, transcode_it->value) == 0 )
		{
			lav_close(ctx);
			return 1;
		}
		transcode_it = transcode_it->next;
	}
	transcode_it = transcode_audio_codecs;
	while( ac && transcode_it )
	{
		struct AVCodec * codec = avcodec_find_decoder(ac->codec_id);
		if ( strcmp(codec->name, transcode_it->value) == 0 )
		{
			lav_close(ctx);
			return 1;
		}
		transcode_it = transcode_it->next;
	}
	
	lav_close(ctx);
	return 0;
}
