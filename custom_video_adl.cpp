/**************************************************************

	custom_video_adl.cpp - ATI/AMD ADL library

	---------------------------------------------------------

	SwitchRes	Modeline generation engine for emulation

	License     GPL-2.0+
	Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

//	Constants and structures ported from AMD ADL SDK files

#include <windows.h>
#include <stdio.h>
#include "custom_video_adl.h"
#include "log.h"


//============================================================
//  memory allocation callbacks
//============================================================

void* __stdcall ADL_Main_Memory_Alloc(int iSize)
{
	void* lpBuffer = malloc(iSize);
	return lpBuffer;
}

void __stdcall ADL_Main_Memory_Free(void** lpBuffer)
{
	if (NULL != *lpBuffer)
	{
		free(*lpBuffer);
		*lpBuffer = NULL;
	}
}

//============================================================
//  adl_timing::adl_timing
//============================================================

adl_timing::adl_timing(char *display_name, char *device_key)
{
	strcpy (m_display_name, display_name);
	strcpy (m_device_key, device_key);
}

//============================================================
//  adl_timing::init
//============================================================

bool adl_timing::init()
{
	int ADL_Err = ADL_ERR;

	log_verbose("ATI/AMD ADL init\n");

	ADL_Err = open();
	if (ADL_Err != ADL_OK)
	{
		log_verbose("ERROR: ADL Initialization error!\n");
		return false;
	}

	ADL_Adapter_NumberOfAdapters_Get = (ADL_ADAPTER_NUMBEROFADAPTERS_GET) (void *) GetProcAddress(hDLL,"ADL_Adapter_NumberOfAdapters_Get");
	if (ADL_Adapter_NumberOfAdapters_Get == NULL)
	{
		log_verbose("ERROR: ADL_Adapter_NumberOfAdapters_Get not available!");
		return false;
	}
	ADL_Adapter_AdapterInfo_Get = (ADL_ADAPTER_ADAPTERINFO_GET) (void *) GetProcAddress(hDLL,"ADL_Adapter_AdapterInfo_Get");
	if (ADL_Adapter_AdapterInfo_Get == NULL)
	{
		log_verbose("ERROR: ADL_Adapter_AdapterInfo_Get not available!");
		return false;
	}
	ADL_Display_DisplayInfo_Get = (ADL_DISPLAY_DISPLAYINFO_GET) (void *) GetProcAddress(hDLL,"ADL_Display_DisplayInfo_Get");
	if (ADL_Display_DisplayInfo_Get == NULL)
	{
		log_verbose("ERROR: ADL_Display_DisplayInfo_Get not available!");
		return false;
	}
	ADL_Display_ModeTimingOverride_Get = (ADL_DISPLAY_MODETIMINGOVERRIDE_GET) (void *) GetProcAddress(hDLL,"ADL_Display_ModeTimingOverride_Get");
	if (ADL_Display_ModeTimingOverride_Get == NULL)
	{
		log_verbose("ERROR: ADL_Display_ModeTimingOverride_Get not available!");
		return false;
	}
	ADL_Display_ModeTimingOverride_Set = (ADL_DISPLAY_MODETIMINGOVERRIDE_SET) (void *) GetProcAddress(hDLL,"ADL_Display_ModeTimingOverride_Set");
	if (ADL_Display_ModeTimingOverride_Set == NULL)
	{
		log_verbose("ERROR: ADL_Display_ModeTimingOverride_Set not available!");
		return false;
	}
	ADL_Display_ModeTimingOverrideList_Get = (ADL_DISPLAY_MODETIMINGOVERRIDELIST_GET) (void *) GetProcAddress(hDLL,"ADL_Display_ModeTimingOverrideList_Get");
	if (ADL_Display_ModeTimingOverrideList_Get == NULL)
	{
		log_verbose("ERROR: ADL_Display_ModeTimingOverrideList_Get not available!");
		return false;
	}

	if (!enum_displays())
	{
		log_error("ADL error enumerating displays.\n");
		return false;
	}

	get_driver_version(m_device_key);

	log_verbose("ADL functions retrieved successfully.\n");
	return true;
}

//============================================================
//  adl_timing::adl_open
//============================================================

int adl_timing::open()
{
	ADL_MAIN_CONTROL_CREATE ADL_Main_Control_Create;
	int ADL_Err = ADL_ERR;

	hDLL = LoadLibraryA("atiadlxx.dll");
	if (hDLL == NULL) hDLL = LoadLibraryA("atiadlxy.dll");

	if (hDLL != NULL)
	{
		ADL_Main_Control_Create = (ADL_MAIN_CONTROL_CREATE) (void *) GetProcAddress(hDLL, "ADL_Main_Control_Create");
		if (ADL_Main_Control_Create != NULL)
				ADL_Err = ADL_Main_Control_Create(ADL_Main_Memory_Alloc, 1);
	}
	else
	{
		log_verbose("ADL Library not found!\n");
	}

	return ADL_Err;
}

//============================================================
//  adl_timing::close
//============================================================

void adl_timing::close()
{
	ADL_MAIN_CONTROL_DESTROY ADL_Main_Control_Destroy;

	log_verbose("ATI/AMD ADL close\n");

	for (int i = 0; i <= iNumberAdapters - 1; i++)
		ADL_Main_Memory_Free((void **)&lpAdapter[i].m_display_list);

	ADL_Main_Memory_Free((void **)&lpAdapterInfo);
	ADL_Main_Memory_Free((void **)&lpAdapter);

	ADL_Main_Control_Destroy = (ADL_MAIN_CONTROL_DESTROY) (void *) GetProcAddress(hDLL, "ADL_Main_Control_Destroy");
	if (ADL_Main_Control_Destroy != NULL)
		ADL_Main_Control_Destroy();

	FreeLibrary(hDLL);
}

//============================================================
//  adl_timing::get_driver_version
//============================================================

bool adl_timing::get_driver_version(char *device_key)
{
	HKEY hkey;
	bool found = false;

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, device_key, 0, KEY_READ , &hkey) == ERROR_SUCCESS)
	{
		BYTE cat_ver[32];
		DWORD length = sizeof(cat_ver);
		if ((RegQueryValueExA(hkey, "Catalyst_Version", NULL, NULL, cat_ver, &length) == ERROR_SUCCESS) ||
			(RegQueryValueExA(hkey, "RadeonSoftwareVersion", NULL, NULL, cat_ver, &length) == ERROR_SUCCESS))
		{
			found = true;
			sscanf((char *)cat_ver, "%d.%d", &cat_version, &sub_version);
			log_verbose("AMD driver version %d.%d\n", cat_version, sub_version);
		}
		RegCloseKey(hkey);
	}
	return found;
}

//============================================================
//  adl_timing::enum_displays
//============================================================

bool adl_timing::enum_displays()
{
	ADL_Adapter_NumberOfAdapters_Get(&iNumberAdapters);

	lpAdapterInfo = (LPAdapterInfo)malloc(sizeof(AdapterInfo) * iNumberAdapters);
	memset(lpAdapterInfo, '\0', sizeof(AdapterInfo) * iNumberAdapters);
	ADL_Adapter_AdapterInfo_Get(lpAdapterInfo, sizeof(AdapterInfo) * iNumberAdapters);

	lpAdapter = (LPAdapterList)malloc(sizeof(AdapterList) * iNumberAdapters);
	for (int i = 0; i <= iNumberAdapters - 1; i++)
	{
		lpAdapter[i].m_index = lpAdapterInfo[i].iAdapterIndex;
		lpAdapter[i].m_bus   = lpAdapterInfo[i].iBusNumber;
		memcpy(&lpAdapter[i].m_name, &lpAdapterInfo[i].strAdapterName, ADL_MAX_PATH);
		memcpy(&lpAdapter[i].m_display_name, &lpAdapterInfo[i].strDisplayName, ADL_MAX_PATH);
		lpAdapter[i].m_num_of_displays = 0;
		lpAdapter[i].m_display_list = 0;

		// Only get display info from target adapter (this api is very slow!)
		if (!strcmp(lpAdapter[i].m_display_name, m_display_name))
			ADL_Display_DisplayInfo_Get(lpAdapter[i].m_index, &lpAdapter[i].m_num_of_displays, &lpAdapter[i].m_display_list, 1);
	}
	return true;
}

//============================================================
//  adl_timing::get_device_mapping_from_display_name
//============================================================

bool adl_timing::get_device_mapping_from_display_name(int *adapter_index, int *display_index)
{
	for (int i = 0; i <= iNumberAdapters -1; i++)
	{
		if (!strcmp(m_display_name, lpAdapter[i].m_display_name))
		{
			ADLDisplayInfo *display_list;
			display_list = lpAdapter[i].m_display_list;

			for (int j = 0; j <= lpAdapter[i].m_num_of_displays - 1; j++)
			{
				if (lpAdapter[i].m_index == display_list[j].displayID.iDisplayLogicalAdapterIndex)
				{
					*adapter_index = lpAdapter[i].m_index;
					*display_index = display_list[j].displayID.iDisplayLogicalIndex;
					return true;
				}
			}
		}
	}
	return false;   
}

//============================================================
//  adl_timing::display_mode_info_to_modeline
//============================================================

bool adl_timing::display_mode_info_to_modeline(ADLDisplayModeInfo *dmi, modeline *m)
{
	if (dmi->sDetailedTiming.sHTotal == 0) return false;

	ADLDetailedTiming dt;
	memcpy(&dt, &dmi->sDetailedTiming, sizeof(ADLDetailedTiming));

	if (dt.sHTotal == 0) return false;

	m->htotal    = dt.sHTotal;
	m->hactive   = dt.sHDisplay;
	m->hbegin    = dt.sHSyncStart;
	m->hend      = dt.sHSyncWidth + m->hbegin;
	m->vtotal    = dt.sVTotal;
	m->vactive   = dt.sVDisplay;
	m->vbegin    = dt.sVSyncStart;
	m->vend      = dt.sVSyncWidth + m->vbegin;
	m->interlace = (dt.sTimingFlags & ADL_DL_TIMINGFLAG_INTERLACED)? 1 : 0;
	m->hsync     = ((dt.sTimingFlags & ADL_DL_TIMINGFLAG_H_SYNC_POLARITY)? 1 : 0) ^ invert_pol(1);
	m->vsync     = ((dt.sTimingFlags & ADL_DL_TIMINGFLAG_V_SYNC_POLARITY)? 1 : 0) ^ invert_pol(1) ;
	m->pclock    = dt.sPixelClock * 10000;

	m->height  = m->height? m->height : dmi->iPelsHeight;
	m->width   = m->width? m->width : dmi->iPelsWidth;
	m->refresh = m->refresh? m->refresh : dmi->iRefreshRate / interlace_factor(m->interlace, 1);;
	m->hfreq = float(m->pclock / m->htotal);
	m->vfreq = float(m->hfreq / m->vtotal) * (m->interlace? 2 : 1);

	return true;
}

//============================================================
//  adl_timing::get_timing
//============================================================

bool adl_timing::get_timing(modeline *m)
{
	int adapter_index = 0;
	int display_index = 0;
	ADLDisplayMode mode_in;
	ADLDisplayModeInfo mode_info_out;
	modeline m_temp = *m;

	//modeline to ADLDisplayMode
	mode_in.iPelsHeight       = m->height;
	mode_in.iPelsWidth        = m->width;
	mode_in.iBitsPerPel       = 32;
	mode_in.iDisplayFrequency = m->refresh * interlace_factor(m->interlace, 1);

	if (!get_device_mapping_from_display_name(&adapter_index, &display_index)) return false;
	if (ADL_Display_ModeTimingOverride_Get(adapter_index, display_index, &mode_in, &mode_info_out) != ADL_OK) goto not_found;
	if (display_mode_info_to_modeline(&mode_info_out, &m_temp))
	{
		if (m_temp.interlace == m->interlace)
		{
			memcpy(m, &m_temp, sizeof(modeline));
			m->type |= CUSTOM_VIDEO_TIMING_ATI_ADL | (!(m->type & MODE_DESKTOP)? V_FREQ_EDITABLE :0);
			return true;
		}
	}
	not_found:
	return false;
}

//============================================================
//  adl_timing::set_timing
//============================================================

bool adl_timing::set_timing(modeline *m, int update_mode)
{
	int adapter_index = 0;
	int display_index = 0;
	ADLDisplayModeInfo mode_info;
	ADLDetailedTiming *dt;
	modeline m_temp;

	//modeline to ADLDisplayModeInfo
	mode_info.iTimingStandard   = (update_mode & MODELINE_DELETE)? ADL_DL_MODETIMING_STANDARD_DRIVER_DEFAULT : ADL_DL_MODETIMING_STANDARD_CUSTOM;
	mode_info.iPossibleStandard = 0;
	mode_info.iRefreshRate      = m->refresh * interlace_factor(m->interlace, 0);
	mode_info.iPelsWidth        = m->width;
	mode_info.iPelsHeight       = m->height;

	//modeline to ADLDetailedTiming
	dt = &mode_info.sDetailedTiming;
	dt->sTimingFlags     = (m->interlace? ADL_DL_TIMINGFLAG_INTERLACED : 0) |
						   (m->hsync ^ invert_pol(0)? ADL_DL_TIMINGFLAG_H_SYNC_POLARITY : 0) |
						   (m->vsync ^ invert_pol(0)? ADL_DL_TIMINGFLAG_V_SYNC_POLARITY : 0);
	dt->sHTotal          = m->htotal;
	dt->sHDisplay        = m->hactive;
	dt->sHSyncStart      = m->hbegin;
	dt->sHSyncWidth      = m->hend - m->hbegin;
	dt->sVTotal          = m->vtotal;
	dt->sVDisplay        = m->vactive;
	dt->sVSyncStart      = m->vbegin;
	dt->sVSyncWidth      = m->vend - m->vbegin;
	dt->sPixelClock      = m->pclock / 10000;
	dt->sHOverscanRight  = 0;
	dt->sHOverscanLeft   = 0;
	dt->sVOverscanBottom = 0;
	dt->sVOverscanTop    = 0;

	if (!get_device_mapping_from_display_name(&adapter_index, &display_index)) return false;
	if (ADL_Display_ModeTimingOverride_Set(adapter_index, display_index, &mode_info, (update_mode & MODELINE_UPDATE_LIST)? 1 : 0) != ADL_OK) return false;

	// read modeline to trigger timing refresh on modded drivers
	memcpy(&m_temp, m, sizeof(modeline));
	if (update_mode & MODELINE_UPDATE) get_timing(&m_temp);

	return true;
}
