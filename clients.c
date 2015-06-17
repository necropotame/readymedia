/* MiniDLNA media server
 * Copyright (C) 2013  NETGEAR
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
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "clients.h"
#include "getifaddr.h"
#include "log.h"

struct client_type_s client_types[] =
{
	{ 0,
	  0,
	  "Unknown",
	  NULL,
	  EMatchNone,
	  NULL
	},

	{ EXbox,
	  FLAG_MIME_AVI_AVI | FLAG_MS_PFS,
	  "Xbox 360",
	  "Xbox/",
	  EUserAgent,
	  NULL
	},

	{ EPS3,
	  FLAG_DLNA | FLAG_MIME_AVI_DIVX,
	  "PLAYSTATION 3",
	  "PLAYSTATION",
	  EUserAgent,
	  NULL
	},

	{ EPS3,
	  FLAG_DLNA | FLAG_MIME_AVI_DIVX,
	  "PLAYSTATION 3",
	  "PLAYSTATION 3",
	  EXAVClientInfo,
	  NULL
	},

	/* Samsung Series [CDE] BDPs and TVs must be separated, or some of our
	 * advertised extra features trigger a folder browsing bug on BDPs. */
	/* User-Agent: DLNADOC/1.50 SEC_HHP_BD-D5100/1.0 */
	{ ESamsungSeriesCDEBDP,
	  FLAG_SAMSUNG | FLAG_DLNA | FLAG_NO_RESIZE,
	  "Samsung Series [CDEF] BDP",
	  "SEC_HHP_BD",
	  EUserAgent
	},

	/* User-Agent: DLNADOC/1.50 SEC_HHP_[TV]UE40D7000/1.0 */
	/* User-Agent: DLNADOC/1.50 SEC_HHP_ Family TV/1.0 */
	{ ESamsungSeriesCDE,
	  FLAG_SAMSUNG | FLAG_DLNA | FLAG_NO_RESIZE | FLAG_SAMSUNG_DCM10,
	  "Samsung Series [CDEF]",
	  "SEC_HHP_",
	  EUserAgent,
	  NULL
	},

	{ ESamsungSeriesA,
	  FLAG_SAMSUNG | FLAG_DLNA | FLAG_NO_RESIZE,
	  "Samsung Series A",
	  "SamsungWiselinkPro",
	  EUserAgent,
	  NULL
	},

	{ ESamsungSeriesB,
	  FLAG_SAMSUNG | FLAG_DLNA | FLAG_NO_RESIZE,
	  "Samsung Series B",
	  "Samsung DTV DMR",
	  EModelName,
	  NULL
	},

	/* User-Agent: Panasonic MIL DLNA CP UPnP/1.0 DLNADOC/1.50 */
	{ EPanasonic,
	  FLAG_DLNA | FLAG_FORCE_SORT,
	  "Panasonic",
	  "Panasonic",
	  EUserAgent,
	  NULL
	},

	/* User-Agent: IPI/1.0 UPnP/1.0 DLNADOC/1.50 */
	{ ENetFrontLivingConnect,
	  FLAG_DLNA | FLAG_FORCE_SORT | FLAG_CAPTION_RES,
	  "NetFront Living Connect",
	  "IPI/1",
	  EUserAgent
	},

	{ EDenonReceiver,
	  FLAG_DLNA,
	  "Denon Receiver",
	  "bridgeCo-DMP/3",
	  EUserAgent,
	  NULL
	},

	{ EFreeBox,
	  FLAG_RESIZE_THUMBS,
	  "FreeBox",
	  "fbxupnpav/",
	  EUserAgent,
	  NULL
	},

	{ EPopcornHour,
	  FLAG_MIME_FLAC_FLAC,
	  "Popcorn Hour",
	  "SMP8634",
	  EUserAgent,
	  NULL
	},

	/* X-AV-Client-Info: av=5.0; cn="Sony Corporation"; mn="Blu-ray Disc Player"; mv="2.0" */
	/* X-AV-Client-Info: av=5.0; cn="Sony Corporation"; mn="BLU-RAY HOME THEATRE SYSTEM"; mv="2.0"; */
	/* Sony SMP-100 needs the same treatment as their BDP-S370 */
	/* X-AV-Client-Info: av=5.0; cn="Sony Corporation"; mn="Media Player"; mv="2.0" */
	{ ESonyBDP,
	  FLAG_DLNA,
	  "Sony BDP",
	  "mv=\"2.0\"",
	  EXAVClientInfo,
	  NULL
	},

	/* User-Agent: Linux/2.6.31-1.0 UPnP/1.0 DLNADOC/1.50 INTEL_NMPR/2.0 LGE_DLNA_SDK/1.5.0 */
	{ ELGDevice,
	  FLAG_DLNA | FLAG_CAPTION_RES,
	  "LG",
	  "LGE_DLNA_SDK",
	  EUserAgent,
	  NULL
	},

	/* X-AV-Client-Info: av=5.0; cn="Sony Corporation"; mn="BRAVIA KDL-40EX503"; mv="1.7"; */
	{ ESonyBravia,
	  FLAG_DLNA,
	  "Sony Bravia",
	  "BRAVIA",
	  EXAVClientInfo,
	  NULL
	},

	/* X-AV-Client-Info: av=5.0; hn=""; cn="Sony Corporation"; mn="INTERNET TV NSX-40GT 1"; mv="0.1"; */
	{ ESonyInternetTV,
	  FLAG_DLNA,
	  "Sony Internet TV",
	  "INTERNET TV",
	  EXAVClientInfo,
	  NULL
	},

	{ ENetgearEVA2000,
	  FLAG_MS_PFS | FLAG_RESIZE_THUMBS,
	  "EVA2000",
	  "Verismo,",
	  EUserAgent,
	  NULL
	},

	{ EDirecTV,
	  FLAG_RESIZE_THUMBS,
	  "DirecTV",
	  "DIRECTV ",
	  EUserAgent,
	  NULL
	},

	{ EToshibaTV,
	  FLAG_DLNA,
	  "Toshiba TV",
	  "UPnP/1.0 DLNADOC/1.50 Intel_SDK_for_UPnP_devices/1.2",
	  EUserAgent,
	  NULL
	},

	{ ERokuSoundBridge,
	  FLAG_MS_PFS | FLAG_AUDIO_ONLY | FLAG_MIME_WAV_WAV | FLAG_FORCE_SORT,
	  "Roku SoundBridge",
	  "Roku SoundBridge",
	  EModelName,
	  NULL
	},

	{ EMarantzDMP,
	  FLAG_DLNA | FLAG_MIME_WAV_WAV,
	  "marantz DMP",
	  "marantz DMP",
	  EFriendlyNameSSDP,
	  NULL
	},

	{ EMediaRoom,
	  FLAG_MS_PFS,
	  "MS MediaRoom",
	  "Microsoft-IPTV-Client",
	  EUserAgent,
	  NULL
	},

	{ ELifeTab,
	  FLAG_MS_PFS,
	  "LIFETAB",
	  "LIFETAB",
	  EFriendlyName,
	  NULL
	},

	{ EAsusOPlay,
	  FLAG_DLNA | FLAG_MIME_AVI_AVI | FLAG_CAPTION_RES,
	  "Asus OPlay Mini/Mini+",
	  "O!Play",
	  EUserAgent,
	  NULL
	},

	{ EBubbleUPnP,
	  FLAG_CAPTION_RES,
	  "BubbleUPnP",
	  "BubbleUPnP",
	  EUserAgent,
	  NULL
	},

	{ EStandardDLNA150,
	  FLAG_DLNA | FLAG_MIME_AVI_AVI,
	  "Generic DLNA 1.5",
	  "DLNADOC/1.50",
	  EUserAgent,
	  NULL
	},

	{ EStandardUPnP,
	  0,
	  "Generic UPnP 1.0",
	  "UPnP/1.0",
	  EUserAgent,
	  NULL
	},

	{ 0, 0, NULL, NULL, 0, NULL }
};

struct client_cache_s clients[CLIENT_CACHE_SLOTS];

struct client_cache_s *
SearchClientCache(struct in_addr addr, int quiet)
{
	int i;

	for (i = 0; i < CLIENT_CACHE_SLOTS; i++)
	{
		if (clients[i].addr.s_addr == addr.s_addr)
		{
			/* Invalidate this client cache if it's older than 1 hour */
			if ((time(NULL) - clients[i].age) > 3600)
			{
				unsigned char mac[6];
				if (get_remote_mac(addr, mac) == 0 &&
				    memcmp(mac, clients[i].mac, 6) == 0)
				{
					/* Same MAC as last time when we were able to identify the client,
					 * so extend the timeout by another hour. */
					clients[i].age = time(NULL);
				}
				else
				{
					memset(&clients[i], 0, sizeof(struct client_cache_s));
					return NULL;
				}
			}
			if (!quiet)
				DPRINTF(E_DEBUG, L_HTTP, "Client found in cache. [%s/entry %d]\n",
					clients[i].type->name, i);
			return &clients[i];
		}
	}

	return NULL;
}

struct client_cache_s *
AddClientCache(struct in_addr addr, int type)
{
	int i;

	for (i = 0; i < CLIENT_CACHE_SLOTS; i++)
	{
		if (clients[i].addr.s_addr)
			continue;
		get_remote_mac(addr, clients[i].mac);
		clients[i].addr = addr;
		clients[i].type = &client_types[type];
		clients[i].age = time(NULL);
		DPRINTF(E_DEBUG, L_HTTP, "Added client [%s/%s/%02X:%02X:%02X:%02X:%02X:%02X] to cache slot %d.\n",
					client_types[type].name, inet_ntoa(clients[i].addr),
					clients[i].mac[0], clients[i].mac[1], clients[i].mac[2],
					clients[i].mac[3], clients[i].mac[4], clients[i].mac[5], i);
		return &clients[i];
	}

	return NULL;
}

