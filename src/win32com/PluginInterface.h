#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include "..\ext\dmddevice\dmddevice.h"
//#include <list>


typedef int(*Open_t)();
typedef bool(*Close_t)();
typedef void(*PM_GameSettings_t)(const char* GameName, UINT64 HardwareGeneration, const tPMoptions &Options);
typedef void(*Set_4_Colors_Palette_t)(rgb24 color0, rgb24 color33, rgb24 color66, rgb24 color100);
typedef void(*Console_Data_t)(UINT8 data);
typedef int(*Console_Input_t)(UINT8 *buf, int size);
typedef int(*Console_Input_Ptr_t)(Console_Input_t ptr);
typedef void(*Render_16_Shades_t)(UINT16 width, UINT16 height, UINT8 *currbuffer);
typedef void(*Render_4_Shades_t)(UINT16 width, UINT16 height, UINT8 *currbuffer);
typedef void(*render_PM_Alphanumeric_Frame_t)(layout_t layout, const UINT16 *const seg_data, const UINT16 *const seg_data2);


class PluginInstance
{
public:
	Open_t DmdDev_Open;
	Close_t DmdDev_Close;
	PM_GameSettings_t DmdDev_PM_GameSettings;
	Set_4_Colors_Palette_t DmdDev_Set_4_Colors_Palette;
	Console_Data_t DmdDev_Console_Data;
	Console_Input_Ptr_t DmdDev_Console_Input_Ptr;
	Render_16_Shades_t DmdDev_Render_16_Shades;
	Render_4_Shades_t DmdDev_Render_4_Shades;
	render_PM_Alphanumeric_Frame_t DmdDev_render_PM_Alphanumeric_Frame;
};


class PluginManager
{
	//list<PluginInstance *> m_Plugins;
	
	int Open();
	bool Close();
	void Set_4_Colors_Palette(rgb24 color0, rgb24 color33, rgb24 color66, rgb24 color100);
	void Set_16_Colors_Palette(rgb24 *color);
	void PM_GameSettings(const char* GameName, UINT64 HardwareGeneration, const tPMoptions &Options);
	void Render_4_Shades(UINT16 width, UINT16 height, UINT8 *currbuffer);
	void Render_16_Shades(UINT16 width, UINT16 height, UINT8 *currbuffer);
	void Render_RGB24(UINT16 width, UINT16 height, rgb24 *currbuffer);
	void Render_PM_Alphanumeric_Frame(layout_t, const UINT16 *const seg_data, const UINT16 *const seg_data2);
	void Console_Data(UINT8 data);
};

class PluginManager g_PluginManager;
