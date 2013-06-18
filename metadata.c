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

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <fcntl.h>

#include <wand/MagickWand.h>
#include <libexif/exif-loader.h>
#include <jpeglib.h>
#include <setjmp.h>
#include "libav.h"

#include "upnpglobalvars.h"
#include "tagutils/tagutils.h"
#include "image_utils.h"
#include "upnpreplyparse.h"
#include "tivo_utils.h"
#include "metadata.h"
#include "albumart.h"
#include "dlnameta.h"
#include "utils.h"
#include "sql.h"
#include "log.h"

#if LIBAVCODEC_VERSION_MAJOR < 53
#define AVMEDIA_TYPE_AUDIO CODEC_TYPE_AUDIO
#define AVMEDIA_TYPE_VIDEO CODEC_TYPE_VIDEO
#endif

#if LIBAVUTIL_VERSION_INT < ((50<<16)+(13<<8)+0)
#define av_strerror(x, y, z) snprintf(y, z, "%d", x)
#endif

#define FLAG_TITLE	0x00000001
#define FLAG_ARTIST	0x00000002
#define FLAG_ALBUM	0x00000004
#define FLAG_GENRE	0x00000008
#define FLAG_COMMENT	0x00000010
#define FLAG_CREATOR	0x00000020
#define FLAG_DATE	0x00000040
#define FLAG_DURATION	0x00000200
#define FLAG_RESOLUTION	0x00000400
#define FLAG_BITRATE	0x00000800
#define FLAG_FREQUENCY	0x00001000
#define FLAG_BPS	0x00002000
#define FLAG_CHANNELS	0x00004000
#define FLAG_ROTATION	0x00008000

#if LIBAVFORMAT_VERSION_INT >= ((52<<16)+(31<<8)+0)
# if LIBAVUTIL_VERSION_INT < ((51<<16)+(5<<8)+0) && !defined(FF_API_OLD_METADATA2)
#define AV_DICT_IGNORE_SUFFIX AV_METADATA_IGNORE_SUFFIX
#define av_dict_get av_metadata_get
typedef AVMetadataTag AVDictionaryEntry;
# endif
#endif

void
check_for_captions(const char *path, int64_t detailID)
{
	char *file = malloc(MAXPATHLEN);
	char *id = NULL;

	sprintf(file, "%s", path);
	strip_ext(file);

	/* If we weren't given a detail ID, look for one. */
	if( !detailID )
	{
		id = sql_get_text_field(db, "SELECT ID from DETAILS where (PATH > '%q.' and PATH <= '%q.z')"
		                            " and MIME glob 'video/*' limit 1", file, file);
		if( id )
		{
			//DPRINTF(E_MAXDEBUG, L_METADATA, "New file %s looks like a caption file.\n", path);
			detailID = strtoll(id, NULL, 10);
		}
		else
		{
			//DPRINTF(E_MAXDEBUG, L_METADATA, "No file found for caption %s.\n", path);
			goto no_source_video;
		}
	}

	strcat(file, ".srt");
	if( access(file, R_OK) == 0 )
	{
		sql_exec(db, "INSERT into CAPTIONS"
		             " (ID, PATH) "
		             "VALUES"
		             " (%lld, %Q)", detailID, file);
	}
no_source_video:
	if( id )
		sqlite3_free(id);
	free(file);
}

void
parse_nfo(const char *path, metadata_t *m)
{
	FILE *nfo;
	char buf[65536];
	struct NameValueParserData xml;
	struct stat file;
	size_t nread;
	char *val, *val2;

	if( stat(path, &file) != 0)
	{
		DPRINTF(E_DEBUG, L_METADATA, "Error getting file stats for %s: errno=%d (%s)\n", path, errno, strerror(errno));
		return;
	} else if (file.st_size > 65536 )
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

	//printf("\ttype: %s\n", GetValueFromNameValueList(&xml, "rootElement"));
	val = GetValueFromNameValueList(&xml, "title");
	if( val )
	{
		val2 = GetValueFromNameValueList(&xml, "episodetitle");
		if( val2 )
			xasprintf(&m->title, "%s - %s", val, val2);
		else
			m->title = strdup(val);
	}

	val = GetValueFromNameValueList(&xml, "plot");
	if( val )
		m->comment = strdup(val);

	val = GetValueFromNameValueList(&xml, "capturedate");
	if( val )
		m->date = strdup(val);

	val = GetValueFromNameValueList(&xml, "genre");
	if( val )
	{
		free(m->genre);
		m->genre = strdup(val);
	}

	ClearNameValueList(&xml);
	fclose(nfo);
}

void
free_metadata(metadata_t *m, uint32_t flags)
{
	if( flags & FLAG_TITLE )
		free(m->title);
	if( flags & FLAG_ARTIST )
		free(m->artist);
	if( flags & FLAG_ALBUM )
		free(m->album);
	if( flags & FLAG_GENRE )
		free(m->genre);
	if( flags & FLAG_CREATOR )
		free(m->creator);
	if( flags & FLAG_DATE )
		free(m->date);
	if( flags & FLAG_COMMENT )
		free(m->comment);
	if( flags & FLAG_DURATION )
		free(m->duration);
	if( flags & FLAG_RESOLUTION )
		free(m->resolution);
	if( flags & FLAG_BITRATE )
		free(m->bitrate);
	if( flags & FLAG_FREQUENCY )
		free(m->frequency);
	if( flags & FLAG_BPS )
		free(m->bps);
	if( flags & FLAG_CHANNELS )
		free(m->channels);
	if( flags & FLAG_ROTATION )
		free(m->rotation);
}

int64_t
GetFolderMetadata(const char *name, const char *path, const char *artist, const char *genre, int64_t album_art)
{
	int ret;

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (TITLE, PATH, CREATOR, ARTIST, GENRE, ALBUM_ART) "
	                   "VALUES"
	                   " ('%q', %Q, %Q, %Q, %Q, %lld);",
	                   name, path, artist, artist, genre, album_art);
	if( ret != SQLITE_OK )
		ret = 0;
	else
		ret = sqlite3_last_insert_rowid(db);

	return ret;
}

int64_t
GetAudioMetadata(const char *path, char *name)
{
	char type[4];
	static char lang[6] = { '\0' };
	struct stat file;
	int64_t ret;
	char *esc_tag;
	int i;
	int fd;
	int64_t album_art = 0;
	struct song_metadata song;
	struct dlna_meta_s dlna_metadata;
	metadata_t m;
	uint32_t free_flags = FLAG_DURATION|FLAG_DATE;
	memset(&m, '\0', sizeof(metadata_t));

	if ( stat(path, &file) != 0 )
	{
		DPRINTF(E_DEBUG, L_METADATA, "Error getting file stats for %s: errno=%d (%s)\n", path, errno, strerror(errno));
		return 0;
	}
	strip_ext(name);

	if( ends_with(path, ".mp3") )
	{
		strcpy(type, "mp3");
	}
	else if( ends_with(path, ".m4a") || ends_with(path, ".mp4") ||
	         ends_with(path, ".aac") || ends_with(path, ".m4p") )
	{
		strcpy(type, "aac");
	}
	else if( ends_with(path, ".3gp") )
	{
		strcpy(type, "aac");
	}
	else if( ends_with(path, ".wma") || ends_with(path, ".asf") )
	{
		strcpy(type, "asf");
	}
	else if( ends_with(path, ".flac") || ends_with(path, ".fla") || ends_with(path, ".flc") )
	{
		strcpy(type, "flc");
	}
	else if( ends_with(path, ".wav") )
	{
		strcpy(type, "wav");
	}
	else if( ends_with(path, ".ogg") || ends_with(path, ".oga") )
	{
		strcpy(type, "ogg");
	}
	else if( ends_with(path, ".pcm") )
	{
		strcpy(type, "pcm");
	}
	else
	{
		DPRINTF(E_WARN, L_GENERAL, "Unhandled file extension on %s\n", path);
		return 0;
	}

	if( !(*lang) )
	{
		if( !getenv("LANG") )
			strcpy(lang, "en_US");
		else
			strncpyt(lang, getenv("LANG"), sizeof(lang));
	}

	if( readtags((char *)path, &song, &file, lang, type) != 0 )
	{
		DPRINTF(E_WARN, L_GENERAL, "Cannot extract tags from %s!\n", path);
        	freetags(&song);
		free_metadata(&m, free_flags);
		return 0;
	}

	if( song.year )
		xasprintf(&m.date, "%04d-01-01", song.year);
	xasprintf(&m.duration, "%d:%02d:%02d.%03d",
	                      (song.song_length/3600000),
	                      (song.song_length/60000%60),
	                      (song.song_length/1000%60),
	                      (song.song_length%1000));
	if( song.title && *song.title )
	{
		m.title = trim(song.title);
		if( (esc_tag = escape_tag(m.title, 0)) )
		{
			free_flags |= FLAG_TITLE;
			m.title = esc_tag;
		}
	}
	else
	{
		m.title = name;
	}
	for( i=ROLE_START; i<N_ROLE; i++ )
	{
		if( song.contributor[i] && *song.contributor[i] )
		{
			m.creator = trim(song.contributor[i]);
			if( strlen(m.creator) > 48 )
			{
				m.creator = strdup("Various Artists");
				free_flags |= FLAG_CREATOR;
			}
			else if( (esc_tag = escape_tag(m.creator, 0)) )
			{
				m.creator = esc_tag;
				free_flags |= FLAG_CREATOR;
			}
			m.artist = m.creator;
			break;
		}
	}
	/* If there is a band associated with the album, use it for virtual containers. */
	if( (i != ROLE_BAND) && (i != ROLE_ALBUMARTIST) )
	{
	        if( song.contributor[ROLE_BAND] && *song.contributor[ROLE_BAND] )
		{
			i = ROLE_BAND;
			m.artist = trim(song.contributor[i]);
			if( strlen(m.artist) > 48 )
			{
				m.artist = strdup("Various Artists");
				free_flags |= FLAG_ARTIST;
			}
			else if( (esc_tag = escape_tag(m.artist, 0)) )
			{
				m.artist = esc_tag;
				free_flags |= FLAG_ARTIST;
			}
		}
	}
	if( song.album && *song.album )
	{
		m.album = trim(song.album);
		if( (esc_tag = escape_tag(m.album, 0)) )
		{
			free_flags |= FLAG_ALBUM;
			m.album = esc_tag;
		}
	}
	if( song.genre && *song.genre )
	{
		m.genre = trim(song.genre);
		if( (esc_tag = escape_tag(m.genre, 0)) )
		{
			free_flags |= FLAG_GENRE;
			m.genre = esc_tag;
		}
	}
	if( song.comment && *song.comment )
	{
		m.comment = trim(song.comment);
		if( (esc_tag = escape_tag(m.comment, 0)) )
		{
			free_flags |= FLAG_COMMENT;
			m.comment = esc_tag;
		}
	}

	album_art = find_album_art(path, song.image, song.image_size);

	fd = open(path, O_RDONLY);
	if ( fd < 0 )
	{
		DPRINTF(E_WARN, L_GENERAL, "Cannot open %s to obtain DLNA metadata!\n", path);
		freetags(&song);
		free_metadata(&m, free_flags);
		return 0;
	}
	dlna_metadata = get_dlna_metadata_audio(fd);
	close(fd);

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (PATH, SIZE, TIMESTAMP, DURATION, CHANNELS, BITRATE, SAMPLERATE, DATE,"
	                   "  TITLE, CREATOR, ARTIST, ALBUM, GENRE, COMMENT, DISC, TRACK, DLNA_PN, MIME, ALBUM_ART) "
	                   "VALUES"
	                   " (%Q, %lld, %ld, '%s', %d, %d, %d, %Q, %Q, %Q, %Q, %Q, %Q, %Q, %d, %d, %Q, '%s', %lld);",
	                   path, (long long)file.st_size, file.st_mtime, m.duration, song.channels, song.bitrate, song.samplerate, m.date,
	                   m.title, m.creator, m.artist, m.album, m.genre, m.comment, song.disc, song.track,
	                   dlna_metadata.dlna_pn, dlna_metadata.mime, album_art);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}

	freetags(&song);
	free_metadata(&m, free_flags);
	free_dlna_metadata(&dlna_metadata);

	return ret;
}

int64_t
GetImageMetadata(const char *path, char *name)
{
	ExifData *ed;
	ExifEntry *e = NULL;
	ExifLoader *l;
	int width=0, height=0, thumb=0;
	char make[32], model[64] = {'\0'};
	char b[1024];
	struct stat file;
	MagickWand *magick_wand;
	char *format;
	int fd;
	int64_t ret;
	image_s *imsrc;
	metadata_t m;
	struct dlna_meta_s dlna_metadata;
	uint32_t free_flags = 0xFFFFFFFF;
	memset(&m, '\0', sizeof(metadata_t));

	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "Parsing %s...\n", path);
	if ( stat(path, &file) != 0 )
	{
		DPRINTF(E_DEBUG, L_METADATA, "Error getting file stats for %s: errno=%d (%s)\n", path, errno, strerror(errno));
		return 0;
	}
	strip_ext(name);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * size: %jd\n", file.st_size);

	l = exif_loader_new();
	exif_loader_write_file(l, path);
	ed = exif_loader_get_data(l);
	exif_loader_unref(l);
	if( !ed )
		goto no_exifdata;

	e = exif_content_get_entry (ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_DATE_TIME_ORIGINAL);
	if( e || (e = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], EXIF_TAG_DATE_TIME_DIGITIZED)) )
	{
		m.date = strdup(exif_entry_get_value(e, b, sizeof(b)));
		if( strlen(m.date) > 10 )
		{
			m.date[4] = '-';
			m.date[7] = '-';
			m.date[10] = 'T';
		}
		else {
			free(m.date);
			m.date = NULL;
		}
	}
	else {
		/* One last effort to get the date from XMP */
		image_get_jpeg_date_xmp(path, &m.date);
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * date: %s\n", m.date);

	e = exif_content_get_entry(ed->ifd[EXIF_IFD_0], EXIF_TAG_MAKE);
	if( e )
	{
		strncpyt(make, exif_entry_get_value(e, b, sizeof(b)), sizeof(make));
		e = exif_content_get_entry(ed->ifd[EXIF_IFD_0], EXIF_TAG_MODEL);
		if( e )
		{
			strncpyt(model, exif_entry_get_value(e, b, sizeof(b)), sizeof(model));
			if( !strcasestr(model, make) )
				snprintf(model, sizeof(model), "%s %s", make, exif_entry_get_value(e, b, sizeof(b)));
			m.creator = escape_tag(trim(model), 1);
		}
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * model: %s\n", model);

	e = exif_content_get_entry(ed->ifd[EXIF_IFD_0], EXIF_TAG_ORIENTATION);
	if( e )
	{
		int rotate;
		switch( exif_get_short(e->data, exif_data_get_byte_order(ed)) )
		{
		case 3:
			rotate = 180;
			break;
		case 6:
			rotate = 90;
			break;
		case 8:
			rotate = 270;
			break;
		default:
			rotate = 0;
			break;
		}
		if( rotate )
			xasprintf(&m.rotation, "%d", rotate);
	}

	if( ed->size )
	{
		/* We might need to verify that the thumbnail is 160x160 or smaller */
		if( ed->size > 12000 )
		{
			imsrc = image_new_from_jpeg(NULL, 0, (char *)ed->data, ed->size, 1, ROTATE_NONE);
			if( imsrc )
			{
 				if( (imsrc->width <= 160) && (imsrc->height <= 160) )
					thumb = 1;
				image_free(imsrc);
			}
		}
		else
			thumb = 1;
	}
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * thumbnail: %d\n", thumb);

	exif_data_unref(ed);

no_exifdata:
	MagickWandGenesis();
	magick_wand = NewMagickWand();
	if ( MagickPingImage(magick_wand, path) == MagickFalse )
	{
		DPRINTF(E_DEBUG, L_METADATA, "Cannot read image %s using MagickWand\n", name);
		free_metadata(&m, free_flags);
		return 0;
	}
	width = MagickGetImageWidth(magick_wand);
	height = MagickGetImageHeight(magick_wand);
	format = MagickGetImageFormat(magick_wand);
	DestroyMagickWand(magick_wand);
	
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * resolution: %dx%d\n", width, height);
	if( !width || !height )
	{
		free_metadata(&m, free_flags);
		return 0;
	}
	xasprintf(&m.resolution, "%dx%d", width, height);

	fd = open(path, O_RDONLY);
	if ( fd < 0 )
	{
		DPRINTF(E_WARN, L_GENERAL, "Cannot open %s to obtain DLNA metadata!\n", path);
		free_metadata(&m, free_flags);
		return 0;
	}
	dlna_metadata = get_dlna_metadata_image_res(format, width, height);
	close(fd);
	free(format);

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (PATH, TITLE, SIZE, TIMESTAMP, DATE, RESOLUTION,"
	                    " ROTATION, THUMBNAIL, CREATOR, DLNA_PN, MIME) "
	                   "VALUES"
	                   " (%Q, '%q', %lld, %ld, %Q, %Q, %Q, %d, %Q, %Q, %Q);",
	                   path, name, (long long)file.st_size, file.st_mtime, m.date, m.resolution,
	                   m.rotation, thumb, m.creator, dlna_metadata.dlna_pn, dlna_metadata.mime);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
	}
	free_metadata(&m, free_flags);
	free_dlna_metadata(&dlna_metadata);

	return ret;
}

int64_t
GetVideoMetadata(const char *path, char *name)
{
	struct stat file;
	int ret, i;
	struct tm *modtime;
	AVFormatContext *ctx = NULL;
	AVCodecContext *ac = NULL, *vc = NULL;
	int audio_stream = -1, video_stream = -1;
	char fourcc[4];
	int64_t album_art = 0;
	struct song_metadata video;
	metadata_t m;
	uint32_t free_flags = 0xFFFFFFFF;
	char *path_cpy, *basepath;

	memset(&m, '\0', sizeof(m));
	memset(&video, '\0', sizeof(video));

	//DEBUG DPRINTF(E_DEBUG, L_METADATA, "Parsing video %s...\n", name);
	if (stat(path, &file) != 0 ) {
		DPRINTF(E_DEBUG, L_METADATA, "Error getting file stats for %s: errno=%d (%s)\n", path, errno, strerror(errno));
		return 0;
	}
	strip_ext(name);
	//DEBUG DPRINTF(E_DEBUG, L_METADATA, " * size: %jd\n", file.st_size);

	ret = lav_open(&ctx, path);
	if( ret != 0 )
	{
		char err[128];
		av_strerror(ret, err, sizeof(err));
		DPRINTF(E_WARN, L_METADATA, "Opening %s failed! [%s]\n", path, err);
		return 0;
	}
	//dump_format(ctx, 0, NULL, 0);
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
	path_cpy = strdup(path);
	basepath = basename(path_cpy);
	if( !vc )
	{
		/* This must not be a video file. */
		lav_close(ctx);
		if( !is_audio(path) )
			DPRINTF(E_DEBUG, L_METADATA, "File %s does not contain a video stream.\n", basepath);
		free(path_cpy);
		return 0;
	}

	if( ac )
	{
		xasprintf(&m.frequency, "%u", ac->sample_rate);
		#if LIBAVCODEC_VERSION_INT < (52<<16)
		xasprintf(&m.bps, "%u", ac->bits_per_sample);
		#else
		xasprintf(&m.bps, "%u", ac->bits_per_coded_sample);
		#endif
		xasprintf(&m.channels, "%u", ac->channels);
	}

	int duration, hours, min, sec, ms;
	DPRINTF(E_DEBUG, L_METADATA, "Container: '%s' [%s]\n", ctx->iformat->name, basepath);
	xasprintf(&m.resolution, "%dx%d", vc->width, vc->height);
	if( ctx->bit_rate > 8 )
		xasprintf(&m.bitrate, "%u", ctx->bit_rate / 8);
	if( ctx->duration > 0 ) {
		duration = (int)(ctx->duration / AV_TIME_BASE);
		hours = (int)(duration / 3600);
		min = (int)(duration / 60 % 60);
		sec = (int)(duration % 60);
		ms = (int)(ctx->duration / (AV_TIME_BASE/1000) % 1000);
		xasprintf(&m.duration, "%d:%02d:%02d.%03d", hours, min, sec, ms);
	}

	if( strcmp(ctx->iformat->name, "avi") == 0 )
	{
		if( vc->codec_id == CODEC_ID_MPEG4 )
		{
				fourcc[0] = vc->codec_tag     & 0xff;
				fourcc[1] = vc->codec_tag>>8  & 0xff;
				fourcc[2] = vc->codec_tag>>16 & 0xff;
				fourcc[3] = vc->codec_tag>>24 & 0xff;
			if( memcmp(fourcc, "XVID", 4) == 0 ||
				memcmp(fourcc, "DX50", 4) == 0 ||
				memcmp(fourcc, "DIVX", 4) == 0 )
				xasprintf(&m.creator, "DiVX");
		}
	}

	if( strcmp(ctx->iformat->name, "asf") == 0 )
	{
		if( readtags((char *)path, &video, &file, "en_US", "asf") == 0 )
		{
			if( video.title && *video.title )
			{
				m.title = escape_tag(trim(video.title), 1);
			}
			if( video.genre && *video.genre )
			{
				m.genre = escape_tag(trim(video.genre), 1);
			}
			if( video.contributor[ROLE_TRACKARTIST] && *video.contributor[ROLE_TRACKARTIST] )
			{
				m.artist = escape_tag(trim(video.contributor[ROLE_TRACKARTIST]), 1);
			}
			if( video.contributor[ROLE_ALBUMARTIST] && *video.contributor[ROLE_ALBUMARTIST] )
			{
				m.creator = escape_tag(trim(video.contributor[ROLE_ALBUMARTIST]), 1);
			}
			else
			{
				m.creator = m.artist;
				free_flags &= ~FLAG_CREATOR;
			}
		}
	}
	#ifndef NETGEAR
	#if LIBAVFORMAT_VERSION_INT >= ((52<<16)+(31<<8)+0)
	else if( strcmp(ctx->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2") == 0 )
	{
		if( ctx->metadata )
		{
			AVDictionaryEntry *tag = NULL;

			//DEBUG DPRINTF(E_DEBUG, L_METADATA, "Metadata:\n");
			while( (tag = av_dict_get(ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)) )
			{
				//DEBUG DPRINTF(E_DEBUG, L_METADATA, "  %-16s: %s\n", tag->key, tag->value);
				if( strcmp(tag->key, "title") == 0 )
					m.title = escape_tag(trim(tag->value), 1);
				else if( strcmp(tag->key, "genre") == 0 )
					m.genre = escape_tag(trim(tag->value), 1);
				else if( strcmp(tag->key, "artist") == 0 )
					m.artist = escape_tag(trim(tag->value), 1);
				else if( strcmp(tag->key, "comment") == 0 )
					m.comment = escape_tag(trim(tag->value), 1);
			}
		}
	}
	#endif
	#endif

	struct dlna_meta_s dlna_metadata = get_dlna_metadata_video_ctx(ctx, audio_stream, video_stream);

	lav_close(ctx);

	if( !m.date )
	{
		m.date = malloc(20);
		modtime = localtime(&file.st_mtime);
		strftime(m.date, 20, "%FT%T", modtime);
	}

	if( !m.title )
		m.title = strdup(name);

	album_art = find_album_art(path, video.image, video.image_size);
	freetags(&video);

	ret = sql_exec(db, "INSERT into DETAILS"
	                   " (PATH, SIZE, TIMESTAMP, DURATION, DATE, CHANNELS, BITRATE, SAMPLERATE, RESOLUTION,"
	                   "  TITLE, CREATOR, ARTIST, GENRE, COMMENT, DLNA_PN, MIME, ALBUM_ART) "
	                   "VALUES"
	                   " (%Q, %lld, %ld, %Q, %Q, %Q, %Q, %Q, %Q, '%q', %Q, %Q, %Q, %Q, %Q, '%q', %lld);",
	                   path, (long long)file.st_size, file.st_mtime, m.duration,
	                   m.date, m.channels, m.bitrate, m.frequency, m.resolution,
	                   m.title, m.creator, m.artist, m.genre, m.comment, dlna_metadata.dlna_pn,
	                   dlna_metadata.mime, album_art);
	if( ret != SQLITE_OK )
	{
		fprintf(stderr, "Error inserting details for '%s'!\n", path);
		ret = 0;
	}
	else
	{
		ret = sqlite3_last_insert_rowid(db);
		check_for_captions(path, ret);
	}
	free_metadata(&m, free_flags);
	free_dlna_metadata(&dlna_metadata);
	free(path_cpy);

	return ret;
}
