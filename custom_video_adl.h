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


typedef void* (__stdcall *ADL_MAIN_MALLOC_CALLBACK)(int);
typedef int (*ADL_MAIN_CONTROL_CREATE)(ADL_MAIN_MALLOC_CALLBACK, int);
typedef int (*ADL_MAIN_CONTROL_DESTROY)();
typedef int (*ADL_ADAPTER_NUMBEROFADAPTERS_GET) (int*);
typedef int (*ADL_ADAPTER_ADAPTERINFO_GET) (LPAdapterInfo, int);
typedef int (*ADL_DISPLAY_DISPLAYINFO_GET) (int, int *, ADLDisplayInfo **, int);
typedef int (*ADL_DISPLAY_MODETIMINGOVERRIDE_GET) (int iAdapterIndex, int iDisplayIndex, ADLDisplayMode *lpModeIn, ADLDisplayModeInfo *lpModeInfoOut);
typedef int (*ADL_DISPLAY_MODETIMINGOVERRIDE_SET) (int iAdapterIndex, int iDisplayIndex, ADLDisplayModeInfo *lpMode, int iForceUpdate);
typedef int (*ADL_DISPLAY_MODETIMINGOVERRIDELIST_GET) (int iAdapterIndex, int iDisplayIndex, int iMaxNumOfOverrides, ADLDisplayModeInfo *lpModeInfoList, int *lpNumOfOverrides);


class adl_timing : public custom_video
{
	public:
		adl_timing(char *display_name, char *device_key);
		~adl_timing() {};
		virtual const char *api_name() { return "ATI ADL"; }
		virtual bool init();
		virtual void close();
		virtual bool get_timing(modeline *m);
		virtual bool set_timing(modeline *m, int update_mode);

	private:
		int open();
		bool get_driver_version(char *device_key);
		bool enum_displays();
		bool get_device_mapping_from_display_name(int *adapter_index, int *display_index);
		bool display_mode_info_to_modeline(ADLDisplayModeInfo *dmi, modeline *m);

		char m_display_name[32];
		char m_device_key[128];

		ADL_ADAPTER_NUMBEROFADAPTERS_GET        ADL_Adapter_NumberOfAdapters_Get;
		ADL_ADAPTER_ADAPTERINFO_GET             ADL_Adapter_AdapterInfo_Get;
		ADL_DISPLAY_DISPLAYINFO_GET             ADL_Display_DisplayInfo_Get;
		ADL_DISPLAY_MODETIMINGOVERRIDE_GET      ADL_Display_ModeTimingOverride_Get;
		ADL_DISPLAY_MODETIMINGOVERRIDE_SET      ADL_Display_ModeTimingOverride_Set;
		ADL_DISPLAY_MODETIMINGOVERRIDELIST_GET  ADL_Display_ModeTimingOverrideList_Get;

		HINSTANCE hDLL;
		LPAdapterInfo lpAdapterInfo = NULL;
		LPAdapterList lpAdapter;
		int iNumberAdapters;
		int cat_version;
		int sub_version;

		int invert_pol(bool on_read) { return ((cat_version <= 12) || (cat_version >= 15 && on_read)); }
		int interlace_factor(bool interlace, bool on_read) { return interlace && ((cat_version <= 12) || (cat_version >= 15 && on_read))? 2 : 1; }
};
