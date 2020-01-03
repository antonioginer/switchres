/**************************************************************

	custom_video_adl.h - ATI/AMD ADL library header

	---------------------------------------------------------

	SwitchRes	Modeline generation engine for emulation

	License     GPL-2.0+
	Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

#include "custom_video.h"

//	Constants and structures ported from AMD ADL SDK files

#define ADL_MAX_PATH   256
#define ADL_OK		     0
#define ADL_ERR		    -1

//ADL_DETAILED_TIMING.sTimingFlags
#define ADL_DL_TIMINGFLAG_DOUBLE_SCAN               0x0001
#define ADL_DL_TIMINGFLAG_INTERLACED                0x0002
#define ADL_DL_TIMINGFLAG_H_SYNC_POLARITY           0x0004
#define ADL_DL_TIMINGFLAG_V_SYNC_POLARITY           0x0008

//ADL_DISPLAY_MODE_INFO.iTimingStandard
#define ADL_DL_MODETIMING_STANDARD_CVT              0x00000001 // CVT Standard
#define ADL_DL_MODETIMING_STANDARD_GTF              0x00000002 // GFT Standard
#define ADL_DL_MODETIMING_STANDARD_DMT              0x00000004 // DMT Standard
#define ADL_DL_MODETIMING_STANDARD_CUSTOM           0x00000008 // User-defined standard
#define ADL_DL_MODETIMING_STANDARD_DRIVER_DEFAULT   0x00000010 // Remove Mode from overriden list
#define ADL_DL_MODETIMING_STANDARD_CVT_RB           0x00000020 // CVT-RB Standard   

typedef struct AdapterInfo
{
	int iSize;
	int iAdapterIndex;
	char strUDID[ADL_MAX_PATH];	
	int iBusNumber;
	int iDeviceNumber;
	int iFunctionNumber;
	int iVendorID;
	char strAdapterName[ADL_MAX_PATH];
	char strDisplayName[ADL_MAX_PATH];
	int iPresent;				
	int iExist;
	char strDriverPath[ADL_MAX_PATH];
	char strDriverPathExt[ADL_MAX_PATH];
	char strPNPString[ADL_MAX_PATH];
	int iOSDisplayIndex;	
} AdapterInfo, *LPAdapterInfo;

typedef struct ADLDisplayID
{
	int iDisplayLogicalIndex;
	int iDisplayPhysicalIndex;
	int iDisplayLogicalAdapterIndex;
	int iDisplayPhysicalAdapterIndex;
} ADLDisplayID, *LPADLDisplayID;


typedef struct ADLDisplayInfo
{
	ADLDisplayID displayID; 
	int iDisplayControllerIndex;	
	char strDisplayName[ADL_MAX_PATH];        
	char strDisplayManufacturerName[ADL_MAX_PATH];	
	int iDisplayType; 
	int iDisplayOutputType; 
	int iDisplayConnector; 
	int iDisplayInfoMask; 
	int iDisplayInfoValue; 
} ADLDisplayInfo, *LPADLDisplayInfo;

typedef struct ADLDisplayMode
{
	int iPelsHeight;
	int iPelsWidth;
	int iBitsPerPel;
	int iDisplayFrequency;
} ADLDisplayMode;

typedef struct ADLDetailedTiming
{
	int   iSize;
	short sTimingFlags;
	short sHTotal;
	short sHDisplay;
	short sHSyncStart;
	short sHSyncWidth;
	short sVTotal;
	short sVDisplay;
	short sVSyncStart;
	short sVSyncWidth;
	unsigned short sPixelClock;
	short sHOverscanRight;
	short sHOverscanLeft;
	short sVOverscanBottom;
	short sVOverscanTop;
	short sOverscan8B;
	short sOverscanGR;
} ADLDetailedTiming;

typedef struct ADLDisplayModeInfo
{
	int iTimingStandard;
	int iPossibleStandard;
	int iRefreshRate;
	int iPelsWidth;
	int iPelsHeight;
	ADLDetailedTiming sDetailedTiming;
} ADLDisplayModeInfo;

typedef struct AdapterList
{
	int m_index;
	int m_bus;
	char m_name[ADL_MAX_PATH];
	char m_display_name[ADL_MAX_PATH];
	int m_num_of_displays;
	ADLDisplayInfo *m_display_list;
} AdapterList, *LPAdapterList;


void adl_close();
bool adl_get_modeline(char *target_display, modeline *m);
bool adl_set_modeline(char *target_display, modeline *m, int update_mode);


class adl_timing : public custom_video
{
	public:
		adl_timing(char *device_name, char *device_key);
		bool init();

	private:
		char m_device_name[32];
		char m_device_key[128];
};
