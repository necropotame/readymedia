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

#include "config.h"

#include <sys/stat.h>
#include <unistd.h>
#include <wand/MagickWand.h>

#include "libav.h"

#include "upnpglobalvars.h"
#include "upnpreplyparse.h"
#include "dlnameta.h"
#include "utils.h"
#include "log.h"
#include "metadata.h"

/* Audio profile flags */
enum audio_profiles {
	PROFILE_AUDIO_UNKNOWN,
	PROFILE_AUDIO_MP3,
	PROFILE_AUDIO_AC3,
	PROFILE_AUDIO_WMA_BASE,
	PROFILE_AUDIO_WMA_FULL,
	PROFILE_AUDIO_WMA_PRO,
	PROFILE_AUDIO_MP2,
	PROFILE_AUDIO_PCM,
	PROFILE_AUDIO_AAC,
	PROFILE_AUDIO_AAC_MULT5,
	PROFILE_AUDIO_AMR
};

/* the mime part of the parse_nfo from metadata.c */
static void
parse_nfo(const char *path, struct dlna_meta_s *m)
{
	FILE *nfo;
	char buf[65536];
	struct NameValueParserData xml;
	struct stat file;
	size_t nread;
	char *val, *val2;

	if( stat(path, &file) != 0 ||
	    file.st_size > 65536 )
	{
		DPRINTF(E_INFO, L_METADATA, "Not parsing very large .nfo file %s\n", path);
		return;
	}
	DPRINTF(E_DEBUG, L_METADATA, "Parsing .nfo file: %s\n", path);
	nfo = fopen(path, "r");
	if( !nfo )
		return;
	nread = fread(&buf, 1, sizeof(buf), nfo);
	
	ParseNameValue(buf, nread, &xml, 0);

	val = GetValueFromNameValueList(&xml, "mime");
	if( val )
	{
		free(m->mime);
		char *esc_tag = unescape_tag(val, 1);
		m->mime = escape_tag(esc_tag, 1);
		free(esc_tag);
	}

	ClearNameValueList(&xml);
	fclose(nfo);
}

/* This function shamelessly copied from libdlna */
#define MPEG_TS_SYNC_CODE 0x47
#define MPEG_TS_PACKET_LENGTH 188
#define MPEG_TS_PACKET_LENGTH_DLNA 192 /* prepends 4 bytes to TS packet */
int
dlna_timestamp_is_present(const char *filename, int *raw_packet_size)
{
	unsigned char buffer[3*MPEG_TS_PACKET_LENGTH_DLNA];
	int fd, i;

	/* read file header */
	fd = open(filename, O_RDONLY);
	if( fd < 0 )
		return 0;
	i = read(fd, buffer, MPEG_TS_PACKET_LENGTH_DLNA*3);
	close(fd);
	if( i < 0 )
		return 0;
	for( i = 0; i < MPEG_TS_PACKET_LENGTH_DLNA; i++ )
	{
		if( buffer[i] == MPEG_TS_SYNC_CODE )
		{
			if (buffer[i + MPEG_TS_PACKET_LENGTH_DLNA] == MPEG_TS_SYNC_CODE &&
			    buffer[i + MPEG_TS_PACKET_LENGTH_DLNA*2] == MPEG_TS_SYNC_CODE)
			{
			        *raw_packet_size = MPEG_TS_PACKET_LENGTH_DLNA;
				if (buffer[i+MPEG_TS_PACKET_LENGTH] == 0x00 &&
				    buffer[i+MPEG_TS_PACKET_LENGTH+1] == 0x00 &&
				    buffer[i+MPEG_TS_PACKET_LENGTH+2] == 0x00 &&
				    buffer[i+MPEG_TS_PACKET_LENGTH+3] == 0x00)
					return 0;
				else
					return 1;
			} else if (buffer[i + MPEG_TS_PACKET_LENGTH] == MPEG_TS_SYNC_CODE &&
				   buffer[i + MPEG_TS_PACKET_LENGTH*2] == MPEG_TS_SYNC_CODE) {
			    *raw_packet_size = MPEG_TS_PACKET_LENGTH;
			    return 0;
			}
		}
	}
	*raw_packet_size = 0;
	return 0;
}

void
free_dlna_metadata(struct dlna_meta_s *m)
{
	if (m->mime)
		free(m->mime);
	if (m->dlna_pn)
		free(m->dlna_pn);
}

struct dlna_meta_s
get_dlna_metadata_image(int fd)
{
	struct dlna_meta_s m = {0, 0};
	int width=0, height=0;
	MagickWand *magick_wand;
	char *format;

	MagickWandGenesis();
	magick_wand = NewMagickWand();
	if ( MagickReadImageFile(magick_wand, fdopen(fd, "rb")) == MagickFalse )
	{
		DPRINTF(E_DEBUG, L_METADATA, "Cannot read file descriptor %d using MagickWand\n", fd);
		return m;
	}
	width = MagickGetImageWidth(magick_wand);
	height = MagickGetImageHeight(magick_wand);
	format = MagickGetImageFormat(magick_wand);
	DestroyMagickWand(magick_wand);

	m = get_dlna_metadata_image_res(format, width, height);
	free(format);
	
	return m;
}

struct dlna_meta_s
get_dlna_metadata_image_res(char* format, int width, int height)
{
	struct dlna_meta_s m = {0, 0};

	if ( strcmp(format, "JPEG") == 0 )
	{
		xasprintf(&m.mime, "image/jpeg");

		if( width <= 640 && height <= 480 )
			xasprintf(&m.dlna_pn, "JPEG_SM");
		else if( width <= 1024 && height <= 768 )
			xasprintf(&m.dlna_pn, "JPEG_MED");
		else if( (width <= 4096 && height <= 4096) || !GETFLAG(DLNA_STRICT_MASK) )
			xasprintf(&m.dlna_pn, "JPEG_LRG");
	}
	else if ( strcmp(format, "PNG") == 0 )
	{
		xasprintf(&m.mime, "image/png");
		/* NOTE: these may be wrong because I don't have DLNA specifications, but I guess
		   it's the same as for JPEG */
		if( width <= 640 && height <= 480 )
			xasprintf(&m.dlna_pn, "PNG_SM");
		else if( width <= 1024 && height <= 768 )
			xasprintf(&m.dlna_pn, "PNG_MED");
		else if( (width <= 4096 && height <= 4096) || !(GETFLAG(DLNA_STRICT_MASK)) )
			xasprintf(&m.dlna_pn, "PNG_LRG");
	}
	else
	{
		/* it is some kind of image, but ve do not know what
		   so we only set mime to indicate it's an image */
		xasprintf(&m.mime, "image/unknown");
	}

	return m;
}

struct dlna_meta_s
get_dlna_metadata_audio(int fd)
{
	int ret;
	AVFormatContext *ctx = NULL;
	struct AVCodecContext *ac = NULL;
	char input_pipe[32];
	int i;
	struct dlna_meta_s m = {0, 0};

	snprintf(input_pipe, sizeof(input_pipe), "pipe:%d", fd);

	ret = lav_open(&ctx, input_pipe);
	if( ret != 0 )
	{
		char err[128];
		av_strerror(ret, err, sizeof(err));
		DPRINTF(E_WARN, L_METADATA, "Opening %s failed! [%s]\n", input_pipe, err);
		return m;
	}
	for( i=0; i<ctx->nb_streams; i++)
	{
		if(ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO )
		{
			ac = ctx->streams[i]->codec;
			break;
		}
	}

	switch (ac->codec_id)
	{
		case AV_CODEC_ID_MP3:
			xasprintf(&m.mime, "audio/mpeg");
			xasprintf(&m.dlna_pn, "MP3");
			break;
		case AV_CODEC_ID_AAC:
			if ( strcmp(ctx->iformat->name, "3gp") == 0 )
				xasprintf(&m.mime, "audio/3gp");
			else
			{
				xasprintf(&m.mime, "audio/mp4");

				/* this may be replaced by ac->profile test against FF_PROFILE_AAC_MAIN, FF_PROFILE_AAC_LOW etc */
				if( ac->extradata_size && ac->extradata )
				{
					uint8_t data;
					memcpy(&data, ac->extradata, 1);
					switch( data >> 3 )
					{
						case AAC_LC:
						case AAC_LC_ER:
							if( ac->sample_rate < 8000 || ac->sample_rate > 48000 )
							{
								DPRINTF(E_DEBUG, L_METADATA, "Unsupported AAC: sample rate is not 8000 < %d < 48000\n",
															ac->sample_rate);
								break;
							}
							/* AAC @ Level 1/2 */
							if( ac->channels <= 2 && ac->bit_rate <= 320000 )
								xasprintf(&m.dlna_pn, "AAC_ISO_320");
							else if( ac->channels <= 2 && ac->bit_rate <= 576000 )
								xasprintf(&m.dlna_pn, "AAC_ISO");
							else if( ac->channels <= 6 && ac->bit_rate <= 1440000 )
								xasprintf(&m.dlna_pn, "AAC_MULT5_ISO");
							else
								DPRINTF(E_DEBUG, L_METADATA, "Unhandled AAC: %d channels, %d bitrate\n",
															ac->channels, ac->bit_rate);
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "Unhandled AAC type %d [%s]\n", data >> 3);
							break;
					}
				}
			}
			break;
		case AV_CODEC_ID_WMAV1:
		case AV_CODEC_ID_WMAV2:
			xasprintf(&m.mime, "audio/x-ms-wma");
			if( ac->rc_max_rate < 193000 )
				xasprintf(&m.dlna_pn, "WMABASE");
			else if( ac->rc_max_rate < 385000 )
				xasprintf(&m.dlna_pn, "WMAFULL");
			break;
		case AV_CODEC_ID_WMAPRO:
			xasprintf(&m.mime, "audio/x-ms-wma");
			xasprintf(&m.dlna_pn, "WMAPRO");
			break;
		case AV_CODEC_ID_WMALOSSLESS:
			xasprintf(&m.mime, "audio/x-ms-wma");
			xasprintf(&m.dlna_pn, "WMALSL%s",
				ac->channels > 2 ? "_MULT5" : "");
			break;
		case AV_CODEC_ID_FLAC:
			xasprintf(&m.mime, "audio/x-flac");
			break;
		case AV_CODEC_ID_VORBIS:
			xasprintf(&m.mime, "audio/ogg");
			break;
		case AV_CODEC_ID_PCM_S16LE:
			/* there are many other PCM codecs, but only this one has mime audio/L16 */
			xasprintf(&m.mime, "audio/L16;rate=%d;channels=%d", ac->sample_rate, ac->channels);
			xasprintf(&m.dlna_pn, "LPCM");
			break;
		/* TODO: AV_CODEC_ID_MP2, AV_CODEC_ID_AMR_NB  */
		default:
			/* handle wav */
			if ( strcmp(ctx->iformat->name, "wav" ) == 0)
				xasprintf(&m.mime, "audio/x-wav");
			else
				DPRINTF(E_DEBUG, L_METADATA, "Unhandled audio codec [0x%X]\n", ac->codec_id);
			return m;
	}

	lav_close(ctx);

	return m;
}

struct dlna_meta_s
get_dlna_metadata_video(int fd)
{
	int ret;
	AVFormatContext *ctx = NULL;
	int audio_stream = -1, video_stream = -1;
	char input_pipe[32];
	int i;
	struct dlna_meta_s metadata = {0, 0};

	snprintf(input_pipe, sizeof(input_pipe), "pipe:%d", fd);

	DPRINTF(E_WARN, L_METADATA, "Opening %s\n", input_pipe);

	ret = lav_open(&ctx, input_pipe);
	if( ret != 0 )
	{
		char err[128];
		av_strerror(ret, err, sizeof(err));
		DPRINTF(E_WARN, L_METADATA, "Opening %s failed! [%s]\n", input_pipe, err);
		return metadata;
	}
	DPRINTF(E_WARN, L_METADATA, "DONE\n");
	for( i=0; i<ctx->nb_streams; i++)
	{
		if( audio_stream == -1 &&
			ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO )
		{
			audio_stream = i;
			continue;
		}
		else if( video_stream == -1 &&
			ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO )
		{
			video_stream = i;
			continue;
		}
	}

	metadata = get_dlna_metadata_video_ctx(ctx, audio_stream, video_stream);
	lav_close(ctx);

	return metadata;
}

struct dlna_meta_s
get_dlna_metadata_video_ctx(struct AVFormatContext *ctx, int audio_stream, int video_stream)
{
	struct dlna_meta_s m = {0, 0};

	struct AVCodecContext *ac = NULL;
	struct AVCodecContext *vc = NULL;
	if (audio_stream >= 0)
	{
		ac = ctx->streams[audio_stream]->codec;
	}
	if (video_stream >= 0)
	{
		vc = ctx->streams[video_stream]->codec;
	}
	else
	{
		/* This must not be a video file. */
		DPRINTF(E_DEBUG, L_METADATA, "File does not contain a video stream.\n");
		return m;
	}
	enum audio_profiles audio_profile = PROFILE_AUDIO_UNKNOWN;
	const char *path = ctx->filename;
	char *path_cpy = strdup(path);
	const char *basepath = basename(path_cpy);
	char nfo[PATH_MAX], *ext;

	if( ac )
	{
		aac_object_type_t aac_type = AAC_INVALID;
		switch( ac->codec_id )
		{
			case AV_CODEC_ID_MP3:
				audio_profile = PROFILE_AUDIO_MP3;
				break;
			case AV_CODEC_ID_AAC:
				if( !ac->extradata_size ||
				    !ac->extradata )
				{
					DPRINTF(E_DEBUG, L_METADATA, "No AAC type\n");
				}
				else
				{
					uint8_t data;
					memcpy(&data, ac->extradata, 1);
					aac_type = data >> 3;
				}
				switch( aac_type )
				{
					/* AAC Low Complexity variants */
					case AAC_LC:
					case AAC_LC_ER:
						if( ac->sample_rate < 8000 ||
						    ac->sample_rate > 48000 )
						{
							DPRINTF(E_DEBUG, L_METADATA, "Unsupported AAC: sample rate is not 8000 < %d < 48000\n",
								ac->sample_rate);
							break;
						}
						/* AAC @ Level 1/2 */
						if( ac->channels <= 2 &&
						    ac->bit_rate <= 576000 )
							audio_profile = PROFILE_AUDIO_AAC;
						else if( ac->channels <= 6 &&
							 ac->bit_rate <= 1440000 )
							audio_profile = PROFILE_AUDIO_AAC_MULT5;
						else
							DPRINTF(E_DEBUG, L_METADATA, "Unhandled AAC: %d channels, %d bitrate\n",
								ac->channels,
								ac->bit_rate);
						break;
					default:
						DPRINTF(E_DEBUG, L_METADATA, "Unhandled AAC type [%d]\n", aac_type);
						break;
				}
				break;
			case AV_CODEC_ID_AC3:
			case AV_CODEC_ID_DTS:
				audio_profile = PROFILE_AUDIO_AC3;
				break;
			case AV_CODEC_ID_WMAV1:
			case AV_CODEC_ID_WMAV2:
				/* WMA Baseline: stereo, up to 48 KHz, up to 192,999 bps */
				if ( ac->bit_rate <= 193000 )
					audio_profile = PROFILE_AUDIO_WMA_BASE;
				/* WMA Full: stereo, up to 48 KHz, up to 385 Kbps */
				else if ( ac->bit_rate <= 385000 )
					audio_profile = PROFILE_AUDIO_WMA_FULL;
				break;
			case AV_CODEC_ID_WMAPRO:
				audio_profile = PROFILE_AUDIO_WMA_PRO;
				break;
			case AV_CODEC_ID_MP2:
				audio_profile = PROFILE_AUDIO_MP2;
				break;
			case AV_CODEC_ID_AMR_NB:
				audio_profile = PROFILE_AUDIO_AMR;
				break;
			default:
				if( (ac->codec_id >= AV_CODEC_ID_PCM_S16LE) &&
				    (ac->codec_id < AV_CODEC_ID_ADPCM_IMA_QT) )
					audio_profile = PROFILE_AUDIO_PCM;
				else
					DPRINTF(E_DEBUG, L_METADATA, "Unhandled audio codec [0x%X]\n", ac->codec_id);
				break;
		}
	}

	int off;
	ts_timestamp_t ts_timestamp = NONE;
	DPRINTF(E_DEBUG, L_METADATA, "Container: '%s' [%s]\n", ctx->iformat->name, basepath);

	/* NOTE: The DLNA spec only provides for ASF (WMV), TS, PS, and MP4 containers.
	 * Skip DLNA parsing for everything else. */
	if ( strcmp(ctx->iformat->name, "asf") != 0 && /* ASF (WMV) */
	     strcmp(ctx->iformat->name, "mpegts") != 0 && /* TS */
	     strcmp(ctx->iformat->name, "mpegtsraw") != 0 && /* TS, not handled yet */
	     strcmp(ctx->iformat->name, "mpeg") != 0 && /* should be similar to PS */
	     strcmp(ctx->iformat->name, "dvd") != 0 && /* PS */
	     strcmp(ctx->iformat->name, "svcd") != 0 && /* PS */
	     strcmp(ctx->iformat->name, "vob") != 0 && /* PS */
	     strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") != 0 /* MP4 */
	) {
		goto video_no_dlna;
	}

	switch( vc->codec_id )
	{
		case AV_CODEC_ID_MPEG1VIDEO:
			if( strcmp(ctx->iformat->name, "mpeg") == 0 )
			{
				if( (vc->width  == 352) &&
				    (vc->height <= 288) )
				{
					m.dlna_pn = strdup("MPEG1");
				}
				xasprintf(&m.mime, "video/mpeg");
			}
			break;
		case AV_CODEC_ID_MPEG2VIDEO:
			m.dlna_pn = malloc(64);
			off = sprintf(m.dlna_pn, "MPEG_");
			if( strcmp(ctx->iformat->name, "mpegts") == 0 )
			{
				int raw_packet_size;
				int dlna_ts_present = dlna_timestamp_is_present(path, &raw_packet_size);
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is MPEG2 TS packet size %d\n",
					video_stream, basepath, raw_packet_size);
				off += sprintf(m.dlna_pn+off, "TS_");
				if( (vc->width  >= 1280) &&
				    (vc->height >= 720) )
				{
					off += sprintf(m.dlna_pn+off, "HD_NA");
				}
				else
				{
					off += sprintf(m.dlna_pn+off, "SD_");
					if( (vc->height == 576) ||
					    (vc->height == 288) )
						off += sprintf(m.dlna_pn+off, "EU");
					else
						off += sprintf(m.dlna_pn+off, "NA");
				}
				if( raw_packet_size == MPEG_TS_PACKET_LENGTH_DLNA )
				{
					if (dlna_ts_present)
						ts_timestamp = VALID;
					else
						ts_timestamp = EMPTY;
				}
				else if( raw_packet_size != MPEG_TS_PACKET_LENGTH )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Unsupported DLNA TS packet size [%d] (%s)\n",
						raw_packet_size, basepath);
					free(m.dlna_pn);
					m.dlna_pn = NULL;
				}
				switch( ts_timestamp )
				{
					case NONE:
						xasprintf(&m.mime, "video/mpeg");
						if( m.dlna_pn )
							off += sprintf(m.dlna_pn+off, "_ISO");
						break;
					case VALID:
						off += sprintf(m.dlna_pn+off, "_T");
					case EMPTY:
						xasprintf(&m.mime, "video/vnd.dlna.mpeg-tts");
					default:
						break;
				}
			}
			else if( strcmp(ctx->iformat->name, "mpeg") == 0 ||
			         strcmp(ctx->iformat->name, "dvd") == 0 ||
			         strcmp(ctx->iformat->name, "svcd") == 0 ||
			         strcmp(ctx->iformat->name, "vob") == 0
			) {
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is MPEG2 PS\n",
					video_stream, basepath);
				off += sprintf(m.dlna_pn+off, "PS_");
				if( (vc->height == 576) ||
				    (vc->height == 288) )
					off += sprintf(m.dlna_pn+off, "PAL");
				else
					off += sprintf(m.dlna_pn+off, "NTSC");
				xasprintf(&m.mime, "video/mpeg");
			}
			else
			{
				DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s [%s] is non-DLNA MPEG2\n",
					video_stream, basepath, ctx->iformat->name);
				free(m.dlna_pn);
				m.dlna_pn = NULL;
			}
			break;
		case AV_CODEC_ID_H264:
			m.dlna_pn = malloc(128);
			off = sprintf(m.dlna_pn, "AVC_");

			if( strcmp(ctx->iformat->name, "mpegts") == 0 )
			{
				AVRational display_aspect_ratio;
				int fps, interlaced;
				int raw_packet_size;
				int dlna_ts_present = dlna_timestamp_is_present(path, &raw_packet_size);

				off += sprintf(m.dlna_pn+off, "TS_");
				if (vc->sample_aspect_ratio.num) {
					av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
					          vc->width  * vc->sample_aspect_ratio.num,
					          vc->height * vc->sample_aspect_ratio.den,
					          1024*1024);
				}
				fps = lav_get_fps(ctx->streams[video_stream]);
				interlaced = lav_get_interlaced(vc, ctx->streams[video_stream]);
				if( ((((vc->width == 1920 || vc->width == 1440) && vc->height == 1080) ||
				      (vc->width == 720 && vc->height == 480)) && fps == 59 && interlaced) ||
				    ((vc->width == 1280 && vc->height == 720) && fps == 59 && !interlaced) )
				{
					if( (vc->profile == FF_PROFILE_H264_MAIN || vc->profile == FF_PROFILE_H264_HIGH) &&
					    audio_profile == PROFILE_AUDIO_AC3 )
					{
						off += sprintf(m.dlna_pn+off, "HD_60_");
						vc->profile = FF_PROFILE_SKIP;
					}
				}
				else if( ((vc->width == 1920 && vc->height == 1080) ||
				          (vc->width == 1440 && vc->height == 1080) ||
				          (vc->width == 1280 && vc->height ==  720) ||
				          (vc->width ==  720 && vc->height ==  576)) &&
				          interlaced && fps == 50 )
				{
					if( (vc->profile == FF_PROFILE_H264_MAIN || vc->profile == FF_PROFILE_H264_HIGH) &&
					    audio_profile == PROFILE_AUDIO_AC3 )
					{
						off += sprintf(m.dlna_pn+off, "HD_50_");
						vc->profile = FF_PROFILE_SKIP;
					}
				}
				switch( vc->profile )
				{
					case FF_PROFILE_H264_BASELINE:
					case FF_PROFILE_H264_CONSTRAINED_BASELINE:
						off += sprintf(m.dlna_pn+off, "BL_");
						if( vc->width  <= 352 &&
						    vc->height <= 288 &&
						    vc->bit_rate <= 384000 )
						{
							off += sprintf(m.dlna_pn+off, "CIF15_");
							break;
						}
						else if( vc->width  <= 352 &&
						         vc->height <= 288 &&
						         vc->bit_rate <= 3000000 )
						{
							off += sprintf(m.dlna_pn+off, "CIF30_");
							break;
						}
						/* Fall back to Main Profile if it doesn't match a Baseline DLNA profile. */
						else
							off -= 3;
					default:
					case FF_PROFILE_H264_MAIN:
						off += sprintf(m.dlna_pn+off, "MP_");
						if( vc->profile != FF_PROFILE_H264_BASELINE &&
						    vc->profile != FF_PROFILE_H264_CONSTRAINED_BASELINE &&
						    vc->profile != FF_PROFILE_H264_MAIN )
						{
							DPRINTF(E_DEBUG, L_METADATA, "Unknown AVC profile %d; assuming MP. [%s]\n",
								vc->profile, basepath);
						}
						if( vc->width  <= 720 &&
						    vc->height <= 576 &&
						    vc->bit_rate <= 10000000 )
						{
							off += sprintf(m.dlna_pn+off, "SD_");
						}
						else if( vc->width  <= 1920 &&
						         vc->height <= 1152 &&
						         vc->bit_rate <= 20000000 )
						{
							off += sprintf(m.dlna_pn+off, "HD_");
						}
						else
						{
							DPRINTF(E_DEBUG, L_METADATA, "Unsupported h.264 video profile! [%s, %dx%d, %dbps : %s]\n",
								m.dlna_pn, vc->width, vc->height, vc->bit_rate, basepath);
							free(m.dlna_pn);
							m.dlna_pn = NULL;
						}
						break;
					case FF_PROFILE_H264_HIGH:
						off += sprintf(m.dlna_pn+off, "HP_");
						if( vc->width  <= 1920 &&
						    vc->height <= 1152 &&
						    vc->bit_rate <= 30000000 &&
						    audio_profile == PROFILE_AUDIO_AC3 )
						{
							off += sprintf(m.dlna_pn+off, "HD_");
						}
						else
						{
							DPRINTF(E_DEBUG, L_METADATA, "Unsupported h.264 HP video profile! [%dbps, %d audio : %s]\n",
								vc->bit_rate, audio_profile, basepath);
							free(m.dlna_pn);
							m.dlna_pn = NULL;
						}
						break;
					case FF_PROFILE_SKIP:
						break;
				}
				if( !m.dlna_pn )
					break;
				switch( audio_profile )
				{
					case PROFILE_AUDIO_MP3:
						off += sprintf(m.dlna_pn+off, "MPEG1_L3");
						break;
					case PROFILE_AUDIO_AC3:
						off += sprintf(m.dlna_pn+off, "AC3");
						break;
					case PROFILE_AUDIO_AAC:
					case PROFILE_AUDIO_AAC_MULT5:
						off += sprintf(m.dlna_pn+off, "AAC_MULT5");
						break;
					default:
						DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for %s file [%s]\n",
							m.dlna_pn, basepath);
						free(m.dlna_pn);
						m.dlna_pn = NULL;
						break;
				}
				if( !m.dlna_pn )
					break;
				if( raw_packet_size == MPEG_TS_PACKET_LENGTH_DLNA )
				{
					if( vc->profile == FF_PROFILE_H264_HIGH ||
					    dlna_ts_present )
						ts_timestamp = VALID;
					else
						ts_timestamp = EMPTY;
				}
				else if( raw_packet_size != MPEG_TS_PACKET_LENGTH )
				{
					DPRINTF(E_DEBUG, L_METADATA, "Unsupported DLNA TS packet size [%d] (%s)\n",
						raw_packet_size, basepath);
					free(m.dlna_pn);
					m.dlna_pn = NULL;
				}
				switch( ts_timestamp )
				{
					case NONE:
						if( m.dlna_pn )
							off += sprintf(m.dlna_pn+off, "_ISO");
						break;
					case VALID:
						off += sprintf(m.dlna_pn+off, "_T");
					case EMPTY:
						xasprintf(&m.mime, "video/vnd.dlna.mpeg-tts");
					default:
						break;
				}
			}
			else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
			{
				off += sprintf(m.dlna_pn+off, "MP4_");

				switch( vc->profile ) {
				case FF_PROFILE_H264_BASELINE:
				case FF_PROFILE_H264_CONSTRAINED_BASELINE:
					if( vc->width  <= 352 &&
					    vc->height <= 288 )
					{
						if( ctx->bit_rate < 600000 )
							off += sprintf(m.dlna_pn+off, "BL_CIF15_");
						else if( ctx->bit_rate < 5000000 )
							off += sprintf(m.dlna_pn+off, "BL_CIF30_");
						else
							goto mp4_mp_fallback;

						if( audio_profile == PROFILE_AUDIO_AMR )
						{
							off += sprintf(m.dlna_pn+off, "AMR");
						}
						else if( audio_profile == PROFILE_AUDIO_AAC )
						{
							off += sprintf(m.dlna_pn+off, "AAC_");
							if( ctx->bit_rate < 520000 )
							{
								off += sprintf(m.dlna_pn+off, "520");
							}
							else if( ctx->bit_rate < 940000 )
							{
								off += sprintf(m.dlna_pn+off, "940");
							}
							else
							{
								off -= 13;
								goto mp4_mp_fallback;
							}
						}
						else
						{
							off -= 9;
							goto mp4_mp_fallback;
						}
					}
					else if( vc->width  <= 720 &&
					         vc->height <= 576 )
					{
						if( vc->level == 30 &&
						    audio_profile == PROFILE_AUDIO_AAC &&
						    ctx->bit_rate <= 5000000 )
							off += sprintf(m.dlna_pn+off, "BL_L3L_SD_AAC");
						else if( vc->level <= 31 &&
						         audio_profile == PROFILE_AUDIO_AAC &&
						         ctx->bit_rate <= 15000000 )
							off += sprintf(m.dlna_pn+off, "BL_L31_HD_AAC");
						else
							goto mp4_mp_fallback;
					}
					else if( vc->width  <= 1280 &&
					         vc->height <= 720 )
					{
						if( vc->level <= 31 &&
						    audio_profile == PROFILE_AUDIO_AAC &&
						    ctx->bit_rate <= 15000000 )
							off += sprintf(m.dlna_pn+off, "BL_L31_HD_AAC");
						else if( vc->level <= 32 &&
						         audio_profile == PROFILE_AUDIO_AAC &&
						         ctx->bit_rate <= 21000000 )
							off += sprintf(m.dlna_pn+off, "BL_L32_HD_AAC");
						else
							goto mp4_mp_fallback;
					}
					else
						goto mp4_mp_fallback;
					break;
				case FF_PROFILE_H264_MAIN:
				mp4_mp_fallback:
					off += sprintf(m.dlna_pn+off, "MP_");
					/* AVC MP4 SD profiles - 10 Mbps max */
					if( vc->width  <= 720 &&
					    vc->height <= 576 &&
					    vc->bit_rate <= 10000000 )
					{
						sprintf(m.dlna_pn+off, "SD_");
						if( audio_profile == PROFILE_AUDIO_AC3 )
							off += sprintf(m.dlna_pn+off, "AC3");
						else if( audio_profile == PROFILE_AUDIO_AAC ||
						         audio_profile == PROFILE_AUDIO_AAC_MULT5 )
							off += sprintf(m.dlna_pn+off, "AAC_MULT5");
						else if( audio_profile == PROFILE_AUDIO_MP3 )
							off += sprintf(m.dlna_pn+off, "MPEG1_L3");
						else
							m.dlna_pn[13] = '\0';
					}
					else if( vc->width  <= 1280 &&
					         vc->height <= 720 &&
					         vc->bit_rate <= 15000000 &&
					         audio_profile == PROFILE_AUDIO_AAC )
					{
						off += sprintf(m.dlna_pn+off, "HD_720p_AAC");
					}
					else if( vc->width  <= 1920 &&
					         vc->height <= 1080 &&
					         vc->bit_rate <= 21000000 &&
					         audio_profile == PROFILE_AUDIO_AAC )
					{
						off += sprintf(m.dlna_pn+off, "HD_1080i_AAC");
					}
					if( strlen(m.dlna_pn) <= 11 )
					{
						DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for %s file %s\n",
							m.dlna_pn, basepath);
						free(m.dlna_pn);
						m.dlna_pn = NULL;
					}
					break;
				case FF_PROFILE_H264_HIGH:
					if( vc->width  <= 1920 &&
					    vc->height <= 1080 &&
					    vc->bit_rate <= 25000000 &&
					    audio_profile == PROFILE_AUDIO_AAC )
					{
						off += sprintf(m.dlna_pn+off, "HP_HD_AAC");
					}
					break;
				default:
					DPRINTF(E_DEBUG, L_METADATA, "AVC profile [%d] not recognized for file %s\n",
						vc->profile, basepath);
					free(m.dlna_pn);
					m.dlna_pn = NULL;
					break;
				}
			}
			else
			{
				free(m.dlna_pn);
				m.dlna_pn = NULL;
			}
			DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is h.264\n", video_stream, basepath);
			break;
		case AV_CODEC_ID_MPEG4:
			/*fourcc[0] = vc->codec_tag     & 0xff;
			fourcc[1] = vc->codec_tag>>8  & 0xff;
			fourcc[2] = vc->codec_tag>>16 & 0xff;
			fourcc[3] = vc->codec_tag>>24 & 0xff;
			DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is MPEG4 [%c%c%c%c/0x%X]\n",
				video_stream, basepath,
				isprint(fourcc[0]) ? fourcc[0] : '_',
				isprint(fourcc[1]) ? fourcc[1] : '_',
				isprint(fourcc[2]) ? fourcc[2] : '_',
				isprint(fourcc[3]) ? fourcc[3] : '_',
				vc->codec_tag);*/

			if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
			{
				m.dlna_pn = malloc(128);
				off = sprintf(m.dlna_pn, "MPEG4_P2_");

				if( ends_with(path, ".3gp") )
				{
					xasprintf(&m.mime, "video/3gpp");
					switch( audio_profile )
					{
						case PROFILE_AUDIO_AAC:
							off += sprintf(m.dlna_pn+off, "3GPP_SP_L0B_AAC");
							break;
						case PROFILE_AUDIO_AMR:
							off += sprintf(m.dlna_pn+off, "3GPP_SP_L0B_AMR");
							break;
						default:
							DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for MPEG4-P2 3GP/0x%X file %s\n",
							        ac->codec_id, basepath);
							free(m.dlna_pn);
							m.dlna_pn = NULL;
							break;
					}
				}
				else
				{
					if( ctx->bit_rate <= 1000000 &&
					    audio_profile == PROFILE_AUDIO_AAC )
					{
						off += sprintf(m.dlna_pn+off, "MP4_ASP_AAC");
					}
					else if( ctx->bit_rate <= 4000000 &&
					         vc->width  <= 640 &&
					         vc->height <= 480 &&
					         audio_profile == PROFILE_AUDIO_AAC )
					{
						off += sprintf(m.dlna_pn+off, "MP4_SP_VGA_AAC");
					}
					else
					{
						DPRINTF(E_DEBUG, L_METADATA, "Unsupported h.264 video profile! [%dx%d, %dbps]\n",
							vc->width,
							vc->height,
							ctx->bit_rate);
						free(m.dlna_pn);
						m.dlna_pn = NULL;
					}
				}
			}
			break;
		case AV_CODEC_ID_WMV3:
			/* I'm not 100% sure this is correct, but it works on everything I could get my hands on */
			if( vc->extradata_size > 0 )
			{
				if( !((vc->extradata[0] >> 3) & 1) )
					vc->level = 0;
				if( !((vc->extradata[0] >> 6) & 1) )
					vc->profile = 0;
			}
		case AV_CODEC_ID_VC1:
			if( strcmp(ctx->iformat->name, "asf") != 0 )
			{
				DPRINTF(E_DEBUG, L_METADATA, "Skipping DLNA parsing for non-ASF VC1 file %s\n", path);
				break;
			}
			m.dlna_pn = malloc(64);
			off = sprintf(m.dlna_pn, "WMV");
			DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is VC1\n", video_stream, basepath);
			xasprintf(&m.mime, "video/x-ms-wmv");
			if( (vc->width  <= 176) &&
			    (vc->height <= 144) &&
			    (vc->level == 0) )
			{
				off += sprintf(m.dlna_pn+off, "SPLL_");
				switch( audio_profile )
				{
					case PROFILE_AUDIO_MP3:
						off += sprintf(m.dlna_pn+off, "MP3");
						break;
					case PROFILE_AUDIO_WMA_BASE:
						off += sprintf(m.dlna_pn+off, "BASE");
						break;
					default:
						DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVSPLL/0x%X file %s\n",
							audio_profile, basepath);
						free(m.dlna_pn);
						m.dlna_pn = NULL;
						break;
				}
			}
			else if( (vc->width  <= 352) &&
			         (vc->height <= 288) &&
			         (vc->profile == 0) &&
			         (ctx->bit_rate/8 <= 384000) )
			{
				off += sprintf(m.dlna_pn+off, "SPML_");
				switch( audio_profile )
				{
					case PROFILE_AUDIO_MP3:
						off += sprintf(m.dlna_pn+off, "MP3");
						break;
					case PROFILE_AUDIO_WMA_BASE:
						off += sprintf(m.dlna_pn+off, "BASE");
						break;
					default:
						DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVSPML/0x%X file %s\n",
							audio_profile, basepath);
						free(m.dlna_pn);
						m.dlna_pn = NULL;
						break;
				}
			}
			else if( (vc->width  <= 720) &&
			         (vc->height <= 576) &&
			         (ctx->bit_rate/8 <= 10000000) )
			{
				off += sprintf(m.dlna_pn+off, "MED_");
				switch( audio_profile )
				{
					case PROFILE_AUDIO_WMA_PRO:
						off += sprintf(m.dlna_pn+off, "PRO");
						break;
					case PROFILE_AUDIO_WMA_FULL:
						off += sprintf(m.dlna_pn+off, "FULL");
						break;
					case PROFILE_AUDIO_WMA_BASE:
						off += sprintf(m.dlna_pn+off, "BASE");
						break;
					default:
						DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVMED/0x%X file %s\n",
							audio_profile, basepath);
						free(m.dlna_pn);
						m.dlna_pn = NULL;
						break;
				}
			}
			else if( (vc->width  <= 1920) &&
			         (vc->height <= 1080) &&
			         (ctx->bit_rate/8 <= 20000000) )
			{
				off += sprintf(m.dlna_pn+off, "HIGH_");
				switch( audio_profile )
				{
					case PROFILE_AUDIO_WMA_PRO:
						off += sprintf(m.dlna_pn+off, "PRO");
						break;
					case PROFILE_AUDIO_WMA_FULL:
						off += sprintf(m.dlna_pn+off, "FULL");
						break;
					default:
						DPRINTF(E_DEBUG, L_METADATA, "No DLNA profile found for WMVHIGH/0x%X file %s\n",
							audio_profile, basepath);
						free(m.dlna_pn);
						m.dlna_pn = NULL;
						break;
				}
			}
			break;
		case AV_CODEC_ID_MSMPEG4V3:
			xasprintf(&m.mime, "video/x-msvideo");
		default:
			DPRINTF(E_DEBUG, L_METADATA, "Stream %d of %s is type %d\n",
				video_stream, basepath, vc->codec_id);
			break;
	}

video_no_dlna:
	if( !m.mime )
	{
		if( strcmp(ctx->iformat->name, "avi") == 0 )
			xasprintf(&m.mime, "video/x-msvideo");
		else if( strncmp(ctx->iformat->name, "mpeg", 4) == 0 )
			xasprintf(&m.mime, "video/mpeg");
		else if( strcmp(ctx->iformat->name, "asf") == 0 )
			xasprintf(&m.mime, "video/x-ms-wmv");
		else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
			if( ends_with(path, ".mov") )
				xasprintf(&m.mime, "video/quicktime");
			else
				xasprintf(&m.mime, "video/mp4");
		else if( strncmp(ctx->iformat->name, "matroska", 8) == 0 )
			xasprintf(&m.mime, "video/x-matroska");
		else if( strcmp(ctx->iformat->name, "flv") == 0 )
			xasprintf(&m.mime, "video/x-flv");
		else
			DPRINTF(E_WARN, L_METADATA, "%s: Unhandled format: %s\n", path, ctx->iformat->name);
	}

#ifdef TIVO_SUPPORT
	if( ends_with(path, ".TiVo") && is_tivo_file(path) )
	{
		if( m.dlna_pn )
		{
			free(m.dlna_pn);
			m.dlna_pn = NULL;
		}
		m.mime = realloc(m.mime, 21);
		strcpy(m.mime, "video/x-tivo-mpeg");
	}
#endif

	strcpy(nfo, path);
	ext = strrchr(nfo, '.');
	if( ext )
	{
		strcpy(ext+1, "nfo");
		if( access(nfo, F_OK) == 0 )
		{
			parse_nfo(nfo, &m);
		}
	}

	free(path_cpy);

	return m;
}
