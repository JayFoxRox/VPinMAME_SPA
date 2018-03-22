/************************************************************************************************
Pinball Arcade DLL Loader

Based on Jannik Vogel (JayFoxRox)'s research.  If you like this project, please consider
sending him a donation for his hard work.

https://github.com/JayFoxRox/pba-tools
https://github.com/JayFoxRox/stern-pba-emu

Licensed under the original MAME license.  Other required change, in driver.c:

DRIVER(spagb, 100)

This code is quite messy, it is a quick one-off implementation.  It also contains many hacks applied to a specific
version of SternGB.DLL to give us access to things the TPA interface does not and work around bugs.  

************************************************************************************************/

#include "driver.h"
#include "core.h"
#include "sim.h"
#include "vpintf.h"
#include "video.h"
#include "cpu/at91/at91.h"
#include "sndbrd.h"
#include "dmddevice.h"
#include "mech.h"
#include <stdint.h>
#include "zlib.h"
#include <windows.h>

// Defines
#define SPA_SOUNDFREQ 48000
#define SPA_SNDBUFSIZE SPA_SOUNDFREQ * 70 / 1000
#define GEN_SPA GEN_SAM
#define SPA_GAME_GB 1
//Static Variables
extern int time_to_quit;
void uSleep(const UINT64 u);

/*----------------
/ Local variables
/-----------------*/
struct {
	int vblankCount;
	int diagnosticLed;
	int sw_stb;
	int zc;
	int video_page[2];
	INT16 samplebuf[2][SPA_SNDBUFSIZE];
	INT16 lastsamp[2];
	int sampout;
	int sampnum;
	UINT8 volume[2], mute[2], DAC_mute[2]; //0..127, 0/1
	int pass;
	int coindoor;
	int samVersion; // 1 or 2
	UINT16 value;
	INT16 bank;
	UINT8 miniDMDData[14][16];
	UINT32 solenoidbits[CORE_MODSOL_MAX];
	UINT32 modulated_lights[WOF_MINIDMD_MAX];  // Also used by Tron ramps
	UINT8 modulated_lights_prev_levels[WOF_MINIDMD_MAX];

} spalocals;

uint8_t *spa_fbuffer, *spa_bbuffer, *persistbuf;

int(*stern_set_rom_data_fn)(uint8_t *, int);
int(*stern_set_persistant_data_fn)(void *, int);
int(*stern_get_persistant_data_fn)(void *, int*);
int(*stern_init_fn)(void);
int(*stern_reset_fn)(int);
int(*stern_set_free_play_fn)(void);
int(*stern_set_high_score_fn)(int, char(*)[4], uint64_t *);
int(*stern_get_high_score_fn)(int, char(*)[4], uint64_t *);
int(*stern_set_grand_champion_fn)(char(*)[4], uint64_t *);
int(*stern_get_grand_champion_fn)(char(*)[4], uint64_t *);
int(*stern_get_lcd_frame_fn)(void);
void*(*stern_get_dmd_bbuffer_fn)(void);
void*(*stern_get_dmd_fbuffer_fn)(void);
int(*stern_step_fn)(void);
void*(*stern_get_sound_buffer)(int*);
void(*stern_set_switch_state_fn)(int, int, int);
void(*stern_set_dedicated_switch_state_fn)(int, int, int);
int(*stern_term_fn)(void);
int(*stern_get_coil_state_fn)(int);
int(*stern_get_lamp_state_fn)(int, int);
void(*stern_set_volume_fn)(int, int);

int(*stern_get_lamp_color)(int, int, void *);
int(*stern_get_coil_capture_state_fn)(int);
int(*stern_get_motor_pos_fn)(void);


#if LOG_RAW_SOUND_DATA
FILE *fpSND = NULL;
#endif

#if LOG_TO_SCREEN
#if LOGALL
#define LOG(x) printf x
#else
#define LOG(x)
#endif
#else
#if LOGALL
#define LOG(x) logerror x
#else
#define LOG(x)
#endif
#endif


#define MAKE16BIT(x,y) (((x)<<8)|(y))



/*-------------------------
/ Machine driver constants
/--------------------------*/
/*-- Common Inports for SAM Games, could update for SPA --*/
#define SPA_COMPORTS \
  PORT_START /* 0 */ \
	/*Switch Col. 0*/ \
    COREPORT_BITDEF(  0x0010, IPT_TILT,           KEYCODE_INSERT)  \
    COREPORT_BIT   (  0x0020, "Slam Tilt",        KEYCODE_HOME)  \
    COREPORT_BIT   (  0x0040, "Ticket Notch",     KEYCODE_K)  \
	COREPORT_BIT   (  0x0080, "Dedicated Sw#20",  KEYCODE_L) \
    COREPORT_BIT   (  0x0100, "Back",             KEYCODE_7) \
    COREPORT_BIT   (  0x0200, "Minus",            KEYCODE_8) \
    COREPORT_BIT   (  0x0400, "Plus",             KEYCODE_9) \
    COREPORT_BIT   (  0x0800, "Select",           KEYCODE_0) \
	/*Switch Col. 2*/ \
    COREPORT_BIT   (  0x8000, "Start Button",     KEYCODE_1) \
    COREPORT_BIT   (  0x4000, "Tournament Start", KEYCODE_2) \
    /*Switch Col. 9*/ \
    COREPORT_BITDEF(  0x0001, IPT_COIN1,          KEYCODE_3)  \
    COREPORT_BITDEF(  0x0002, IPT_COIN2,          KEYCODE_4)  \
    COREPORT_BITDEF(  0x0004, IPT_COIN3,          KEYCODE_5)  \
    COREPORT_BITDEF(  0x0008, IPT_COIN4,          KEYCODE_6)  \
	/*None*/ \
    COREPORT_BITTOG(  0x1000, "Coin Door",        KEYCODE_END) \
  PORT_START /* 1 */ \
    COREPORT_DIPNAME( 0x001f, 0x0000, "Country") \
      COREPORT_DIPSET(0x0000, "USA" ) \
      COREPORT_DIPSET(0x000d, "Australia" ) \
      COREPORT_DIPSET(0x0001, "Austria" ) \
      COREPORT_DIPSET(0x0002, "Belgium" ) \
      COREPORT_DIPSET(0x0003, "Canada 1" ) \
      COREPORT_DIPSET(0x001a, "Canada 2" ) \
      COREPORT_DIPSET(0x0013, "Chuck E. Cheese" ) \
      COREPORT_DIPSET(0x0016, "Croatia" ) \
      COREPORT_DIPSET(0x0009, "Denmark" ) \
      COREPORT_DIPSET(0x0005, "Finland" ) \
      COREPORT_DIPSET(0x0006, "France" ) \
      COREPORT_DIPSET(0x0007, "Germany" ) \
      COREPORT_DIPSET(0x000f, "Greece" ) \
      COREPORT_DIPSET(0x0008, "Italy" ) \
      COREPORT_DIPSET(0x001b, "Lithuania" ) \
      COREPORT_DIPSET(0x0015, "Japan" ) \
      COREPORT_DIPSET(0x0017, "Middle East" ) \
      COREPORT_DIPSET(0x0004, "Netherlands" ) \
      COREPORT_DIPSET(0x0010, "New Zealand" ) \
      COREPORT_DIPSET(0x000a, "Norway" ) \
      COREPORT_DIPSET(0x0011, "Portugal" ) \
      COREPORT_DIPSET(0x0019, "Russia" ) \
      COREPORT_DIPSET(0x0014, "South Africa" ) \
      COREPORT_DIPSET(0x0012, "Spain" ) \
      COREPORT_DIPSET(0x000b, "Sweden" ) \
      COREPORT_DIPSET(0x000c, "Switzerland" ) \
      COREPORT_DIPSET(0x0018, "Taiwan" ) \
      COREPORT_DIPSET(0x000e, "U.K." ) \
      COREPORT_DIPSET(0x001c, "Unknown (00011100)" ) \
      COREPORT_DIPSET(0x001d, "Unknown (00011101)" ) \
      COREPORT_DIPSET(0x001e, "Unknown (00011110)" ) \
      COREPORT_DIPSET(0x001f, "Unknown (00011111)" ) \
      COREPORT_DIPNAME( 0x0020, 0x0000, "Dip #6") \
      COREPORT_DIPSET(0x0000, "0" ) \
      COREPORT_DIPSET(0x0020, "1" ) \
      COREPORT_DIPNAME( 0x0040, 0x0000, "Dip #7") \
      COREPORT_DIPSET(0x0000, "0" ) \
      COREPORT_DIPSET(0x0040, "1" ) \
      COREPORT_DIPNAME( 0x0080, 0x0080, "Dip #8") \
      COREPORT_DIPSET(0x0000, "1" ) \
      COREPORT_DIPSET(0x0080, "0" )

/*-- Standard input ports --*/
#define SPA_INPUT_PORTS_START(name,balls) \
  INPUT_PORTS_START(name) \
    CORE_PORTS \
    SIM_PORTS(balls) \
    SPA_COMPORTS \
  INPUT_PORTS_END

#define SPA_COMINPORT CORE_COREINPORT

static void spasnd_init(struct sndbrdData *brdData) { }

//Sound Interface
const struct sndbrdIntf spaIntf = {
	"SPA", spasnd_init, NULL, NULL, man3_w, scmd_w, NULL, NULL, NULL, SNDBRD_NODATASYNC
};

int spa_sndbufferlength()
{
	if (spalocals.sampnum >= spalocals.sampout)
		return spalocals.sampnum - spalocals.sampout;
	else
		return spalocals.sampnum + SPA_SNDBUFSIZE - spalocals.sampout;

}


static void spa_sh_update(int num, INT16 *buffer[2], int length)
{
	if (length > 0)
	{
		int ii = 0, channel;

		for (ii = 0; ii < length && spalocals.sampout != spalocals.sampnum; ii++)
		{
			for (channel = 0; channel < 2; channel++)
			{
				spalocals.lastsamp[channel] = buffer[channel][ii] = spalocals.samplebuf[channel][spalocals.sampout];
			}
			if (++spalocals.sampout == SPA_SNDBUFSIZE)
				spalocals.sampout = 0;
		}

		for (; ii < length; ++ii)
			for (channel = 0; channel < 2; channel++)
				buffer[channel][ii] = spalocals.lastsamp[channel];
	}
}

static int spa_sh_start(const struct MachineSound *msound)
{
	const char *stream_name[] = { "SPA Left", "SPA Right" };
	const int volume[2] = { MIXER(100,MIXER_PAN_LEFT), MIXER(100,MIXER_PAN_RIGHT) };
	/*-- allocate a DAC stream at fixed frequency --*/
	return stream_init_multi
	(2,
		stream_name,
		volume,
		SPA_SOUNDFREQ,
		0,
		spa_sh_update) < 0;
}

static void spa_sh_stop(void) { }

static struct CustomSound_interface spaCustInt =
{
	spa_sh_start,
	spa_sh_stop,
	0,
};

#define HSCOUNT 4

struct nvram
{
	uint8_t freeplay, volume;
};

struct nvram nv;


void reset_nvram()
{
	memset(&nv, 0, sizeof(struct nvram));
	nv.volume = 80;
	nv.freeplay = 1;
}

void critical_error(char *msg)
{
	MessageBox(NULL, msg, NULL, MB_OK | MB_ICONERROR);
	exit(1);
}

char spa_dllpath[MAX_PATH];
uint32_t dllcrc = 0;
uint32_t imagecrc = 0;


// Some very messy references to memory locations within TPA release build DLL, mostly found
// with CheatEngine.

int hsptr[] = { 0x990, 0x9b8, 0xaf0, 0xb40, 0xb90, 0xa70 };
#define knockptr (persistbuf - 0x32000 + hsptr[0] - 0xE4c + 0x28)
#define langptr (knockptr - 0xa0)
#define ballspergameptr (knockptr - 0x30)
#define lightptr (knockptr + 0x28CFC)
#define volptr (persistbuf - 0x32000 + hsptr[0] - 0xE4c + 0x28 + 0x803734C)
#define profanityptr (knockptr - 0xb0)
#define scareptr (knockptr + 0x80)
#define freeplayptr (knockptr - 0x10)
#define flipenableptr (knockptr + 0xDEE)

#define DLLCRC_TPAGBDLL 0x171a7d69
#define DLLCRC_SPAGBDLL 0x2d0ebf1d

static MACHINE_INIT(spa) {
	memset(&spalocals, 0, sizeof(spalocals));
	reset_nvram();

	static int once = 1;

	if (once)
	{
		once = 0;
		// Load the DLL
		char path[MAX_PATH];

#ifdef VPINMAME
		HINSTANCE hInst;
#ifndef _WIN64
		hInst = GetModuleHandle("VPinSPA.dll");
#else
		hInst = GetModuleHandle("VPinSPA64.dll");  // if ever..
#endif
		GetModuleFileName(hInst, spa_dllpath, 1024);
		*strrchr(spa_dllpath, '\\') = 0;
#else
		strcpy(spa_dllpath, "C:\\Visual Pinball\\VPinSPA");
#endif

		sprintf(path, "%s\\spagb100\\SternGB.dll", spa_dllpath);

		FILE* f = fopen(path, "rb");
		if (f == NULL) {
			char tmp[MAX_PATH];
			sprintf(tmp, "Failed to open %s", path);
			critical_error(tmp);
			return;
		}
		fseek(f, 0, SEEK_END);
		size_t size = ftell(f);
		uint8_t* data = malloc(size);
		fseek(f, 0, SEEK_SET);
		fread(data, size, 1, f);
		fclose(f);

		dllcrc = crc32(dllcrc, (UINT8*)data, size);
		static void h_crc_end(UINT8* chksum);
		free(data);
		HMODULE dll = LoadLibrary(path);
		if (dll == NULL) {
			critical_error("Failed to load DLL");
			return;
		}

		// Load the ROM
		sprintf(path, "%s\\spagb100\\image.bin", spa_dllpath);
		f = fopen(path, "rb");
		if (f == NULL) {
			critical_error("Cannot open image.bin");
			return;
		}
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		data = malloc(size);
		fseek(f, 0, SEEK_SET);
		fread(data, size, 1, f);
		fclose(f);
		imagecrc = crc32(imagecrc, (UINT8*)data, size);


		// Resolve some of the exports
		stern_set_rom_data_fn = (int(*)(uint8_t *, int))GetProcAddress(dll, "stern_set_rom_data_fn");
		stern_set_persistant_data_fn = (int(*)(void *, int))GetProcAddress(dll, "stern_set_persistant_data_fn");     // Persistant[sic]!!
		stern_get_persistant_data_fn = (int(*)(void *, int *))GetProcAddress(dll, "stern_get_persistant_data_fn");

		stern_init_fn = (int(*)(void))GetProcAddress(dll, "stern_init_fn");
		stern_reset_fn = (int(*)(int))GetProcAddress(dll, "stern_reset_fn");
		stern_set_free_play_fn = (int(*)(void))GetProcAddress(dll, "stern_set_free_play_fn");
		stern_set_high_score_fn = (int(*)(int, char(*)[4], uint64_t *))GetProcAddress(dll, "stern_set_high_score_fn");
		stern_get_high_score_fn = (int(*)(int, char(*)[4], uint64_t *))GetProcAddress(dll, "stern_get_high_score_fn");
		stern_set_grand_champion_fn = (int(*)(char(*)[4], uint64_t *))GetProcAddress(dll, "stern_set_grand_champion_fn");
		stern_get_grand_champion_fn = (int(*)(char(*)[4], uint64_t *))GetProcAddress(dll, "stern_get_grand_champion_fn");

		stern_get_dmd_bbuffer_fn = (void*(*)(void))GetProcAddress(dll, "stern_get_dmd_bbuffer_fn");
		stern_get_dmd_fbuffer_fn = (void*(*)(void))GetProcAddress(dll, "stern_get_dmd_fbuffer_fn");
		stern_step_fn = (int(*)(void))GetProcAddress(dll, "stern_step_fn");
		stern_get_sound_buffer = (void*(*)(void *))GetProcAddress(dll, "stern_get_sound_buffer");
		stern_set_switch_state_fn = (void(*)(int, int, int))GetProcAddress(dll, "stern_set_switch_state_fn");
		stern_set_dedicated_switch_state_fn = (void(*)(int, int, int))GetProcAddress(dll, "stern_set_dedicated_switch_state_fn");
		stern_term_fn = (int(*)(void))GetProcAddress(dll, "stern_term_fn");
		stern_get_coil_state_fn = (int(*)(int))GetProcAddress(dll, "stern_get_coil_state_fn");
		stern_get_lamp_state_fn = (int(*)(int, int))GetProcAddress(dll, "stern_get_lamp_state_fn");
		stern_set_volume_fn = (void(*)(int, int))GetProcAddress(dll, "stern_set_volume_fn");
		stern_get_lamp_color = (int(*)(int, int, void *))GetProcAddress(dll, "stern_get_lamp_color");

		stern_get_coil_capture_state_fn = (int(*)(int))GetProcAddress(dll, "stern_get_coil_capture_state_fn");
		stern_get_motor_pos_fn = (int(*)(void))GetProcAddress(dll, "stern_get_motor_pos_fn");
		stern_get_lcd_frame_fn = (int(*)(void))GetProcAddress(dll, "stern_get_lcd_frame_fn");

		// It turns out, the very first thing stern_step_fn does is force the profanity mode off.  Fuck that.  :)
		// I thought it would require a DLL patch until I realized I can simply enter the step function later.  
		if (dllcrc == DLLCRC_TPAGBDLL)
			stern_step_fn = (uint8_t *)stern_step_fn + 0x13;

		if (stern_set_rom_data_fn == NULL) {
			critical_error("Cannot find stern_set_rom_data in DLL.");
		}

		// Setup image.bin.
		stern_set_rom_data_fn(data, size);
		stern_init_fn();
		stern_get_persistant_data_fn(&persistbuf, &size);
		spa_bbuffer = stern_get_dmd_bbuffer_fn();
		spa_fbuffer = stern_get_dmd_fbuffer_fn();
	}
}



static MACHINE_RESET(spa) {
	for (int i = 0; i < 8; i++)
		stern_set_dedicated_switch_state_fn(i, 0, core_getDip(0) & (1 << i));
	stern_reset_fn(1);
	for (int i = 0; i < 8; i++)
		stern_set_dedicated_switch_state_fn(i, 0, core_getDip(0) & (1 << i));

	if (dllcrc == DLLCRC_TPAGBDLL)
	{
		// Turn the knocker on
		int *mem = knockptr;
		(*mem) = 2;
		(*(mem+1)) = 253;
	}

	// Load nvram if present
	char path[MAX_PATH];
	sprintf(path, "%s\\spagb100\\gbnv.txt", spa_dllpath);
	FILE *f = fopen(path, "rb");
	if (f != NULL) {
		char tmp[81], *p, *p2, *p3;

		while (!feof(f))
		{
			fgets(tmp, sizeof(tmp), f);
			p = strtok(tmp, ":");
			if (p != NULL)
			{
				p2 = strtok(NULL, ":,");
				if (p2 != NULL)
				{
					if (_stricmp(p, "freeplay") == 0)
					{
						nv.freeplay = atoi(p2);
						if (nv.freeplay)
							stern_set_free_play_fn();
					}
					else if (_stricmp(p, "volume") == 0)
					{
						nv.volume = atoi(p2);
						stern_set_volume_fn(0, nv.volume);
					}
					else if (_strnicmp(p, "language", 7) == 0 && dllcrc == DLLCRC_TPAGBDLL)
					{
						int *mem = langptr;
						(*mem) = atoi(p2);
						(*(mem+1)) = 255-atoi(p2);
					}
					else if (_strnicmp(p, "profanity", 9) == 0 && (dllcrc == DLLCRC_TPAGBDLL))
					{
						int *mem = profanityptr;
						(*mem) = atoi(p2);
						(*(mem + 1)) = 255 - atoi(p2);
					}
					else if (_strnicmp(p, "scare", 9) == 0 && (dllcrc == DLLCRC_TPAGBDLL))
					{
						int *mem = scareptr;
						(*mem) = atoi(p2);
						(*(mem + 1)) = 255 - atoi(p2);
					}
					else if (_strnicmp(p, "ballspergame", 12) == 0 && dllcrc == DLLCRC_TPAGBDLL)
					{
						int *mem = ballspergameptr;
						(*mem) = atoi(p2);
						(*(mem + 1)) = 255 - atoi(p2);
					}
					else if (_strnicmp(p, "hiscore", 7) == 0)
					{
						p3 = strtok(NULL, ":, ");
						if (p3 != NULL)
						{
							int slot = atoi(p + 7);
							char inits[4] = { 0,0,0,0 };
							strncpy(inits, p2, 3);
							uint64_t score = strtoull(p3, NULL, 10);
							switch (slot)
							{
							case 0:
								stern_set_grand_champion_fn(&inits, &score);
								break;
							default:
								if (slot > 4 && slot - 5 < sizeof(hsptr) / sizeof(*hsptr) && dllcrc == DLLCRC_TPAGBDLL)
								{
									*(uint64_t *)(persistbuf - 0x32000 + hsptr[slot - 5] + 0x18) = score;
									strncpy(persistbuf - 0x32000 + hsptr[slot - 5], inits, 3);
								}
								else
									stern_set_high_score_fn(slot - 1, &inits, &score);
								break;
							}
						}
					}
				}
			}
		}
		fclose(f);
	}
	else
	{
		nv.volume = 80;
		nv.freeplay = 1;
		stern_set_free_play_fn();
		stern_set_volume_fn(0, 80);
	}
}

static MACHINE_STOP(spa) {
	//	stern_term_fn();
	time_to_quit = 1;

	char path[MAX_PATH];
	sprintf(path, "%s\\spagb100\\gbnv.txt", spa_dllpath);
	FILE *f = fopen(path, "w");
	if (f)
	{
		if (dllcrc == DLLCRC_TPAGBDLL)
		{
			uint8_t *mem = volptr;
			nv.volume = ((*mem) * 100 / 63)+1;
			mem = freeplayptr;
			nv.freeplay = *mem;
		}
		fprintf(f, "FreePlay:%d\n", nv.freeplay);
		fprintf(f, "Volume:%d\n", nv.volume);
		char inits[4]; 
		uint64_t score;

		stern_get_grand_champion_fn(&inits, &score);
		if (dllcrc == DLLCRC_TPAGBDLL)
		{
			int *mem = langptr;
			fprintf(f, "Language:%d\n", *mem);
			mem = ballspergameptr;
			fprintf(f, "BallsPerGame:%d\n", *mem);
			mem = profanityptr;
			fprintf(f, "Profanity:%d\n", *mem);
			mem = scareptr;
			fprintf(f, "Scare:%d\n", *mem);
		}
		fprintf(f, "HiScore0:%s,%llu\n", inits, score);
		for (int i = 0; i < HSCOUNT; i++)
		{
			stern_get_high_score_fn(i, &inits, &score);
			fprintf(f, "HiScore%d:%s,%llu\n", i + 1, inits, score);
		}
		if (dllcrc == DLLCRC_TPAGBDLL)
		{
			for (int i = 0; i < sizeof(hsptr)/sizeof(*hsptr); i++)
			{
				fprintf(f, "HiScore%d:%s,%llu\n", 5 + i, persistbuf - 0x32000 + hsptr[i], *(uint64_t *)(persistbuf - 0x32000 + hsptr[i] + 0x18));
			}
	
		}
		fclose(f);
	}

#if LOG_RAW_SOUND_DATA
	if (fpSND) fclose(fpSND);
	fpSND = NULL;
#endif
}

static SWITCH_UPDATE(spa) {
	if (inports) {
		// 1111      .... checking 0x000X and putting into column 9
		/*Switch Col 9 = Coin Slots*/
		CORE_SETKEYSW(inports[SPA_COMINPORT], 0x0f, 9);

		// 1111 1111 .... checking 0x0XX0 and putting those into column 0
		/*Switch Col 0 = Tilt, Slam, Coin Door Buttons*/
		CORE_SETKEYSW(inports[SPA_COMINPORT] >> 4, 0xff, 0);

		// 1100 0000 .... checking 0x8000 and 0x4000 and setting switch column 2
		/*Switch Col 2 = Start */
		CORE_SETKEYSW(inports[SPA_COMINPORT] >> 8, 0xc0, 2);

		// 0x1000(coin door) >> 12
		/*Coin Door Switch - Not mapped to the switch matrix*/
		spalocals.coindoor = (inports[SPA_COMINPORT] & 0x1000) ? 0 : 1;

	}
}


static int spa_getSol(int solNo)
{
	return 0;
}

/********************/
/*  VBLANK Section  */
/********************/
static INTERRUPT_GEN(spa_vblank)
{
	core_updateSw(1);

	int i;

	// Switches
	int sw = coreGlobals.swMatrix[0];
	for (i = 8
		; i < 12; i++)
		stern_set_dedicated_switch_state_fn(i, 0, (sw &  (1 << (i - 4))) ? 1 : 0);

	for (i = 0; i < 8; i++)
		stern_set_dedicated_switch_state_fn(i, 0, core_getDip(0) & (1 << i));

	for (i = 1; i < 90; i++)
	{
		stern_set_switch_state_fn(i % 16, i / 16, core_getSw(i));
	}

	// Solenoids
	coreGlobals.solenoids = 0;
	coreGlobals.solenoids2 = 0;
	for (i = 0; i < 32; i++)
	{
		UINT8 value = stern_get_coil_state_fn(i);
		coreGlobals.modulatedSolenoids[CORE_MODSOL_CUR][i] = value;
		if (value)
		{
			coreGlobals.solenoids |= (1u << i);
			// Copy solenoids 2 and 3 to the VPM dedicated flipper locations
			if (i == 2)
				coreGlobals.solenoids2 |= CORE_LLFLIPSOLBITS;
			if (i == 3)
				coreGlobals.solenoids2 |= CORE_LRFLIPSOLBITS;
		}
	}
	if (dllcrc == DLLCRC_TPAGBDLL)
	{
		// See if the flippers are enabled.  If they are, turn on solenoid 33, so VPX
		// can bypass the switch-solenoid delay, which is fairly high on this for some reason. 
		uint8_t *mem = flipenableptr;
		if ((*mem) == 0x03)
			coreGlobals.solenoids2 |= 0x10;
	}
	static int half = 0;
	if (half++ == 1)
		half = 0;
	else
	{
		time_to_quit |= updatescreen();
		memset(&coreGlobals.lampMatrix[0], 0, 42);

		int dst;

		if (dllcrc == DLLCRC_TPAGBDLL) // We can directly read raw light values.   Lets us see the library colors correctly and get full ranges.
		{
			uint8_t *lights = lightptr;
			for (i = 0; i < 166; i++,lights+=4)
			{
				coreGlobals.RGBlamps[i] = *lights;
			}
		}
		else
		{

			for (i = 0, dst = 0; i < 150; i++)
			{
				uint8_t clr[40];
				int clrlast = 0;

				stern_get_lamp_color(i, 0, &clr);
				for (int z = 0; z < 3; z++)
				{
					clr[z] += 0x40;  // pba subtracts this inexplicably.. put it back!
					clr[z] *= 2;
				}

#ifdef DEBUG
				if ((clr[0] || clr[1] || clr[2]) && !(clr[0] == 127 && clr[1] == 127 && clr[2] == 127))
				{
					char s[81];

					sprintf(s, "clrs %d: %d %d %d\n", i, clr[0], clr[1], clr[2]);
					OutputDebugString(s);
				}
#endif

				switch (i)
				{
				case 26:
				case 27:
				case 41:
				case 42:
				case 47:
				case 46:
				case 50:
				case 51:
				case 62:
				case 63:
				case 80:
				case 81:
				case 86:
				case 87:
				case 98:
				case 97:
				case 109:
				case 108:
					// Part of RGB sequences.  Skip.
					break;
				case 45:
					// TPA bug - DLL doesn't give us color of 45 (doesn't see 46 and 47).  Confirmed on my iPad.   Return white for now. 
					coreGlobals.RGBlamps[dst++] = clr[0];
					coreGlobals.RGBlamps[dst++] = clr[0];
					coreGlobals.RGBlamps[dst++] = clr[0];
					break;
				case 28:
				case 43:
				case 52:
				case 64:
				case 82:
				case 88:
					coreGlobals.RGBlamps[dst++] = clr[2];
					coreGlobals.RGBlamps[dst++] = clr[1];
					coreGlobals.RGBlamps[dst++] = clr[0];
					break;
				case 96:
				case 107:
					coreGlobals.RGBlamps[dst++] = clr[0];
					coreGlobals.RGBlamps[dst++] = clr[1];
					coreGlobals.RGBlamps[dst++] = clr[2];
					break;
				case 122:
					coreGlobals.RGBlamps[dst++] = clr[1];
					break;
				default:
					coreGlobals.RGBlamps[dst++] = clr[0];
					break;
				}
			}
		}
		coreGlobals.RGBlamps[131] = coreGlobals.RGBlamps[143];
		coreGlobals.RGBlamps[201] = stern_get_lcd_frame_fn() % 256;
		coreGlobals.RGBlamps[202] = stern_get_lcd_frame_fn() / 256;
	}
	coreGlobals.RGBlamps[123] = coreGlobals.RGBlamps[119]; // Map 123 to 119 since ecto GI is weirdly mapped to 0 along with 110. 

	if (!(dllcrc == DLLCRC_TPAGBDLL))
	{
		// If not the release TPA build as of 3/7/18, use the default canonical implementation
		// which moves too fast.  
		coreGlobals.RGBlamps[200] = stern_get_motor_pos_fn();
	}
	else
	{
		// Filthy dirty disgusting hack to work around TPA's overly fast motor implementation.
		// The get motor pos function does nothing but return the value of an internal variable.  We want to meddle with that variable
		// so get the memory address right after the MOV EAX instruction.
		int8_t **pos = ((int8_t *)stern_get_motor_pos_fn) + 1;
		static int slowness = 1;
		slowness = slowness - 1;
		if (slowness == 0)
		{
			coreGlobals.RGBlamps[200] = **pos;
			slowness = 5;
		}
		else
			**pos = coreGlobals.RGBlamps[200];
	}

	spalocals.vblankCount += 1;
	while (spa_sndbufferlength() < SPA_SNDBUFSIZE - 800)
	{
		int audio_samples = 0;

		int16_t* audio = stern_get_sound_buffer(&audio_samples);

		// Farsight lies!  Per disassmeby this function iterates in 800-byte increments but indicates only 792!
		// This bug even seems present on my iPad.

		if (dllcrc == DLLCRC_TPAGBDLL || dllcrc == DLLCRC_SPAGBDLL)
			audio_samples = 800;
		for (int i = 0; i < audio_samples; i++)
		{
			spalocals.samplebuf[0][spalocals.sampnum] = audio[i];
			spalocals.samplebuf[1][spalocals.sampnum] = audio[i];
			if (++spalocals.sampnum == SPA_SNDBUFSIZE)
				spalocals.sampnum = 0;
		}
	}
	// Goofy things happen if you set switches shortly after calling this, so sleep after to ensure a timing blip doesn't cause this to loop quickly. 
	stern_step_fn();
	Sleep(1);
}

/*-----------------------------------------------
/ Load/Save static ram
/ Save RAM & CMOS Information
/-------------------------------------------------*/
static NVRAM_HANDLER(spa) {
	//core_nvram(file, read_or_write, nvram, 0x20000, 0xff);		//128K NVRAM
}

//Toggle Zero Cross bit
static void spa_timer(int data)
{
}

static INTERRUPT_GEN(spa_irq)
{

}

/*********************************************/
/* Define VPinMame Game for SPA              */
/*********************************************/
static MACHINE_DRIVER_START(spa)
MDRV_IMPORT_FROM(PinMAME)
MDRV_SWITCH_UPDATE(spa)
MDRV_TIMER_ADD(spa_vblank, 120)
MDRV_CORE_INIT_RESET_STOP(spa, spa, spa)
MDRV_DIPS(8)
MDRV_NVRAM_HANDLER(spa)
MDRV_SOUND_ADD(CUSTOM, spaCustInt)
MDRV_SOUND_ATTRIBUTES(SOUND_SUPPORTS_STEREO)
MACHINE_DRIVER_END



#define INITSPAGAME(name, gen, disp, lampcol, hw) \
	static core_tGameData name##GameData = { \
		gen, disp, {FLIP_SW(FLIP_L) | FLIP_SOL(FLIP_L), 0, lampcol, 14, 0, 0, hw,0, spa_getSol}}; \
	static void init_##name(void) { core_gameData = &name##GameData; }

/*****************/
/*  DMD Section  */
/*****************/

/*-- SAM DMD display uses 32 x 128 pixels by accessing 0x1000 bytes per page.
That's 8 bits for each pixel, but they are distributed into 4 brightness
bits (16 colors), and 4 translucency bits that perform the masking of the
secondary or "background" page that will "shine through" if the mask bits
of the foreground are set.    SPA DLL appears to give us this same data.   Current simple
implementation just looks to see if transparent, if so uses background page.  */

static PINMAME_VIDEO_UPDATE(spadmd_update) {
	int y;
	tDMDDot dotCol;

	for (y = 0; y < 32; y++)
	{
		memcpy(&dotCol[y + 1][0], &spa_fbuffer[y * 128], 128);
		for (int x = 0; x < 128; x++)
			if (dotCol[y + 1][x] > 15) // Transparency set
				dotCol[y + 1][x] = spa_bbuffer[y * 128 + x];  
	}

	video_update_core_dmd(bitmap, cliprect, dotCol, layout);
	return 0;
}


static struct core_dispLayout spa_dmd128x32[] = {
	{ 0, 0, 32, 128, CORE_DMD | CORE_DMDNOAA, (genf *)spadmd_update },
	{ 0 }
};



/**********************/
/* ROM LOADING MACROS */
/**********************/

/* Would be good to use this scheme to load image instead of current raw file system... */
#define SPA_ROM32MB(game,rom,chk,len) \
ROM_START(game) \
	ROM_REGION32_LE(0x2000000, REGION_USER1,0) \
	ROM_FILL(0x200000, 0x100000, 0xff) \
ROM_END


/********************/
/* GAME DEFINITIONS */
/********************/


/*-------------------------------------------------------------------
/ Ghostbusters LE
/-------------------------------------------------------------------*/
INITSPAGAME(spagb, GEN_SAM, spa_dmd128x32, SAM_8COL, SPA_GAME_GB)

SPA_ROM32MB(spagb_100, "fg_1200ai.bin", CRC(078b0c9a) SHA1(f1472d2c4a06d674bf652dd481cce5d6ca125e0c), 0x01D9F8B8)
SPA_INPUT_PORTS_START(spagb, 1)
CORE_GAMEDEF(spagb, 100, "Ghostbusters LE", 2013, "Stern", spa, 0)


