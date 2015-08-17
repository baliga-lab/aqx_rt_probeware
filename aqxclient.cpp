/*
 * aqxclient.cpp : Defines the entry point for the console application.
 */
#include <stdio.h>
#include <string.h>
#include <memory.h>

#ifdef TARGET_OS_WIN
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#pragma warning(disable: 4996)
#endif
#ifdef TARGET_OS_LINUX
#include <sys/time.h>
#endif
#ifdef TARGET_OS_MAC
#include <Carbon/Carbon.h>
#endif

extern "C" {
#include "aqxapi_client.h"
}
/*
#include <microhttpd.h>
#define HTTP_PORT 8080
*/

#define MAX_NUM_MEASUREMENTS 100

/* Please replace for user */
#define REFRESH_TOKEN "1/uHlxK48dCAolwIS-FckPhaMcWMKrdO7QVbo9E_Kb_k1IgOrJDtdun6zK6XiATCKT"
#define SYSTEM_UID "7921a6763e0011e5beb064273763ec8b"
#define SEND_INTERVAL_SECS 60

/* TODO: This is actually a feature of the measuring component */
#define SECONDS_PER_SAMPLE 1

#include "GoIO_DLL_interface.h"
#include "NGIO_lib_interface.h"

const char *goio_deviceDesc[8] = {"?", "?", "Go! Temp", "Go! Link", "Go! Motion", "?", "?", "Mini GC"};

NGIO_LIBRARY_HANDLE g_hNGIOlib = NULL;

bool GoIO_GetAvailableDeviceName(char *deviceName, gtype_int32 nameLength, gtype_int32 *pVendorId, gtype_int32 *pProductId);
static void OSSleep(unsigned long msToSleep);

/*
int answer_to_connection (void *cls, struct MHD_Connection *connection,
                          const char *url,
                          const char *method, const char *version,
                          const char *upload_data,
                          size_t *upload_data_size, void **con_cls)
{
  const char *page  = "<html><body>Hello, browser!</body></html>";
  struct MHD_Response *response;
  int ret;

  response = MHD_create_response_from_buffer (strlen (page),
                                            (void*) page, MHD_RESPMEM_PERSISTENT);
  ret = MHD_queue_response (connection, MHD_HTTP_OK, response);
  MHD_destroy_response (response);
  return ret;
}
*/

int init_system()
{
	gtype_uint16 goio_minor, goio_major, ngio_minor, ngio_major;
  struct aqx_client_options aqx_options = {SYSTEM_UID, REFRESH_TOKEN, SEND_INTERVAL_SECS};

	GoIO_Init();
	GoIO_GetDLLVersion(&goio_major, &goio_minor);
	g_hNGIOlib = NGIO_Init();
	NGIO_GetDLLVersion(g_hNGIOlib, &ngio_major, &ngio_minor);

	fprintf(stderr, "aqx_client V0.001 - (c) 2015 Institute for Systems Biology\nGoIO library version %d.%d\nNGIO library version %d.%d\n",
          goio_major, goio_minor, ngio_major, ngio_minor);

  aqx_client_init(&aqx_options);
  return 1;
}

void cleanup_system()
{
  NGIO_Uninit(g_hNGIOlib);
	GoIO_Uninit();
  aqx_client_cleanup();
}

GOIO_SENSOR_HANDLE GoIO_OpenTemperatureDevice()
{
	char deviceName[GOIO_MAX_SIZE_DEVICE_NAME];
  /* USB vendor ids */
	gtype_int32 vendorId, productId;
  GOIO_SENSOR_HANDLE hDevice = NULL;

	bool bFoundDevice = GoIO_GetAvailableDeviceName(deviceName, GOIO_MAX_SIZE_DEVICE_NAME, &vendorId, &productId);
  if (bFoundDevice) {
    hDevice = GoIO_Sensor_Open(deviceName, vendorId, productId, 0);
    char tmpstring[100];

    if (hDevice != NULL) {
      unsigned char charId;

      /* Phase 1 send io request */
      GoIO_Sensor_DDSMem_GetSensorNumber(hDevice, &charId, 0, 0);
      GoIO_Sensor_DDSMem_GetLongName(hDevice, tmpstring, sizeof(tmpstring));

      printf("Successfully opened '%s' device '%s'.\n", goio_deviceDesc[productId], deviceName);
      if (tmpstring && !strncmp("Temperature", tmpstring, sizeof(tmpstring))) {
        fprintf(stderr, "Sensor id = %d (%s)\n", charId, tmpstring);      
      } else {
        /* We did not find the right sensor, so close it and ignore */
        fprintf(stderr, "Did not find the right sensor: '%s'\n", tmpstring);
        GoIO_Sensor_Close(hDevice);
        hDevice = NULL;
      }
    }
  }
  return hDevice;
}

/* Step 1: Send IO request */
void GoIO_SendIORequest(GOIO_SENSOR_HANDLE hDevice)
{
  GoIO_Sensor_SetMeasurementPeriod(hDevice, 0.040, SKIP_TIMEOUT_MS_DEFAULT);//40 milliseconds measurement period.
  GoIO_Sensor_SendCmdAndGetResponse(hDevice, SKIP_CMD_ID_START_MEASUREMENTS, NULL, 0, NULL, NULL, SKIP_TIMEOUT_MS_DEFAULT);
}

/* Step 2: Collect, aggregate and convert measurments */
double GoIO_CollectMeasurement(GOIO_SENSOR_HANDLE hDevice)
{
	gtype_int32 numMeasurements, i;
	gtype_int32 rawMeasurements[MAX_NUM_MEASUREMENTS];
	gtype_real64 volts[MAX_NUM_MEASUREMENTS];
	gtype_real64 calbMeasurements[MAX_NUM_MEASUREMENTS];
	gtype_real64 averageCalbMeasurement;

  numMeasurements = GoIO_Sensor_ReadRawMeasurements(hDevice, rawMeasurements, MAX_NUM_MEASUREMENTS);
  averageCalbMeasurement = 0.0;
  for (i = 0; i < numMeasurements; i++) {
    volts[i] = GoIO_Sensor_ConvertToVoltage(hDevice, rawMeasurements[i]);
    calbMeasurements[i] = GoIO_Sensor_CalibrateData(hDevice, volts[i]);
    averageCalbMeasurement += calbMeasurements[i];
  }
  if (numMeasurements > 1) averageCalbMeasurement = averageCalbMeasurement / numMeasurements;

  /*
  GoIO_Sensor_DDSMem_GetCalibrationEquation(hDevice, &equationType);
  gtype_real32 a, b, c;
  unsigned char activeCalPage = 0;
	char units[20];
	char equationType = 0;
  GoIO_Sensor_DDSMem_GetActiveCalPage(hDevice, &activeCalPage);
  GoIO_Sensor_DDSMem_GetCalPage(hDevice, activeCalPage, &a, &b, &c, units, sizeof(units));
  printf("Average measurement = %8.3f %s .\n", averageCalbMeasurement, units);
  */

  return averageCalbMeasurement;
}

NGIO_DEVICE_HANDLE NGIO_OpenLabQuestDevices(gtype_uint32 *retDeviceType)
{
	gtype_uint32 sig, mask, deviceType;
	gtype_uint32 numDevices;
	NGIO_DEVICE_LIST_HANDLE hDeviceList;
	gtype_int32 status = 0;
	char deviceName[NGIO_MAX_SIZE_DEVICE_NAME];
  NGIO_DEVICE_HANDLE retval = NULL;

  deviceType = NGIO_DEVTYPE_LABQUEST;
  NGIO_SearchForDevices(g_hNGIOlib, deviceType, NGIO_COMM_TRANSPORT_USB, NULL, &sig);  
  hDeviceList = NGIO_OpenDeviceListSnapshot(g_hNGIOlib, deviceType, &numDevices, &sig);
  status = NGIO_DeviceListSnapshot_GetNthEntry(hDeviceList, 0, deviceName, sizeof(deviceName), &mask);
  NGIO_CloseDeviceListSnapshot(hDeviceList);

  fprintf(stderr, "NGIO LabQuest Status: %d\n", status);

  if (status) {
    deviceType = NGIO_DEVTYPE_LABQUEST_MINI;
    NGIO_SearchForDevices(g_hNGIOlib, deviceType, NGIO_COMM_TRANSPORT_USB, NULL, &sig);

    hDeviceList = NGIO_OpenDeviceListSnapshot(g_hNGIOlib, deviceType, &numDevices, &sig);
    status = NGIO_DeviceListSnapshot_GetNthEntry(hDeviceList, 0, deviceName, sizeof(deviceName), &mask);
    NGIO_CloseDeviceListSnapshot(hDeviceList);
    fprintf(stderr, "NGIO LabQuest Mini Status: %d\n", status);
  }
  if (status) {
    deviceType = NGIO_DEVTYPE_LABQUEST2;
    NGIO_SearchForDevices(g_hNGIOlib, deviceType, NGIO_COMM_TRANSPORT_USB, NULL, &sig);

    hDeviceList = NGIO_OpenDeviceListSnapshot(g_hNGIOlib, deviceType, &numDevices, &sig);
    status = NGIO_DeviceListSnapshot_GetNthEntry(hDeviceList, 0, deviceName, sizeof(deviceName), &mask);
    NGIO_CloseDeviceListSnapshot(hDeviceList);
    fprintf(stderr, "NGIO LabQuest2 Status: %d\n", status);
  }

  if (!status) {
    retval = NGIO_Device_Open(g_hNGIOlib, deviceName, 0);
    *retDeviceType = deviceType;
    if (retval) {
      fprintf(stderr, "NGIO Device Type: %d opened successfully\n", deviceType);
    } else {
      fprintf(stderr, "Opening NGIO Device failed !\n");
    }
  }
  return retval;
}

void NGIO_SendIORequest(NGIO_DEVICE_HANDLE hDevice, gtype_uint32 deviceType)
{
	NGIOGetStatusCmdResponsePayload getStatusResponse;
	NGIO_NVMEM_CHANNEL_ID1_rec getNVMemResponse;
	gtype_uint32 nRespBytes;
  gtype_int32 status = 0;

  if ((NGIO_DEVTYPE_LABQUEST == deviceType) || (NGIO_DEVTYPE_LABQUEST2 == deviceType)) {
#if !(defined(TARGET_PLATFORM_LABQUEST) || defined(TARGET_PLATFORM_LABQUEST2))
    /* Wrest control of the LabQuest data acquisition subsystem(the DAQ) away from the GUI app running
       down on the LabQuest. */
    status = NGIO_Device_AcquireExclusiveOwnership(hDevice, NGIO_GRAB_DAQ_TIMEOUT);
    if (status) {
      fprintf(stderr, "NGIO_Device_AcquireExclusiveOwnership() failed!\n");
    }
#endif
  }
  
  if (!status) {
    memset(&getStatusResponse, 0, sizeof(getStatusResponse));
    nRespBytes = sizeof(getStatusResponse);
    status = NGIO_Device_SendCmdAndGetResponse(hDevice, NGIO_CMD_ID_GET_STATUS, NULL, 0, &getStatusResponse,
                                               &nRespBytes, NGIO_TIMEOUT_MS_DEFAULT);
  }

  if (!status) {
    /*
    printf("DAQ firmware version is %x.%02x .\n", (gtype_uint16) getStatusResponse.majorVersionMasterCPU, 
           (gtype_uint16) getStatusResponse.minorVersionMasterCPU);
    */
    memset(&getNVMemResponse, 0, sizeof(getNVMemResponse));
    status = NGIO_Device_NVMemBlk_Read(hDevice, NGIO_NVMEM_CHANNEL_ID1, &getNVMemResponse, 0,
                                       sizeof(getNVMemResponse) - 1, NGIO_TIMEOUT_MS_DEFAULT);
  }

  if (!status) {
    unsigned int serialNum = getNVMemResponse.serialNumber.msbyteMswordSerialCounter;
    serialNum = (serialNum << 8) + getNVMemResponse.serialNumber.lsbyteMswordSerialCounter;
    serialNum = (serialNum << 8) + getNVMemResponse.serialNumber.msbyteLswordSerialCounter;
    serialNum = (serialNum << 8) + getNVMemResponse.serialNumber.lsbyteLswordSerialCounter;
    fprintf(stderr, "LabQuest serial number(yy ww nnnnnnnn) is %02x %02x %08d\n", 
           (gtype_uint16) getNVMemResponse.serialNumber.yy, 
           (gtype_uint16) getNVMemResponse.serialNumber.ww, serialNum);
  }
  if (!status) {
    NGIOSetSensorChannelEnableMaskParams maskParams;
    NGIOSetAnalogInputParams analogInputParams;
    unsigned char channelMask = NGIO_CHANNEL_MASK_ANALOG1;
    signed char channel;
    unsigned char sensorId = 0;
    gtype_uint32 sig;


    memset(&maskParams, 0, sizeof(maskParams));
    for (channel = NGIO_CHANNEL_ID_ANALOG1; channel <= NGIO_CHANNEL_ID_ANALOG4; channel++) {
      NGIO_Device_DDSMem_GetSensorNumber(hDevice, channel, &sensorId, 1, &sig, NGIO_TIMEOUT_MS_DEFAULT);
      if (sensorId != 0) {
        maskParams.lsbyteLsword_EnableSensorChannels = maskParams.lsbyteLsword_EnableSensorChannels | channelMask;
        if (sensorId >= kSensorIdNumber_FirstSmartSensor)
          NGIO_Device_DDSMem_ReadRecord(hDevice, channel, 0, NGIO_TIMEOUT_MS_READ_DDSMEMBLOCK);

        if (kProbeTypeAnalog10V == NGIO_Device_GetProbeType(hDevice, channel))
          analogInputParams.analogInput = NGIO_ANALOG_INPUT_PM10V_BUILTIN_12BIT_ADC;
        else
          analogInputParams.analogInput = NGIO_ANALOG_INPUT_5V_BUILTIN_12BIT_ADC;
        analogInputParams.channel = channel;
        NGIO_Device_SendCmdAndGetResponse(hDevice, NGIO_CMD_ID_SET_ANALOG_INPUT, &analogInputParams, 
                                          sizeof(analogInputParams), NULL, NULL, NGIO_TIMEOUT_MS_DEFAULT);
      }
      channelMask = channelMask << 1;
    }

    if (0 == maskParams.lsbyteLsword_EnableSensorChannels) {
      printf("No analog sensors found.\n");
    } else {
      maskParams.lsbyteLsword_EnableSensorChannels = maskParams.lsbyteLsword_EnableSensorChannels & 14;//spam ignore analog4

      NGIO_Device_SendCmdAndGetResponse(hDevice, NGIO_CMD_ID_SET_SENSOR_CHANNEL_ENABLE_MASK, &maskParams, 
                                        sizeof(maskParams), NULL, NULL, NGIO_TIMEOUT_MS_DEFAULT);

      NGIO_Device_SetMeasurementPeriod(hDevice, -1, 0.001, NGIO_TIMEOUT_MS_DEFAULT);// 1000 hz.
      NGIO_Device_SendCmdAndGetResponse(hDevice, NGIO_CMD_ID_START_MEASUREMENTS, NULL, 0, NULL, NULL, NGIO_TIMEOUT_MS_DEFAULT);
    }
  }
}

void NGIO_CollectMeasurements(NGIO_DEVICE_HANDLE hDevice, struct aqx_measurement *measurement)
{
  signed char channel;
  unsigned char sensorId = 0;
  gtype_uint32 sig;
  gtype_int32 numMeasurements, i;
  gtype_int32 rawMeasurements[MAX_NUM_MEASUREMENTS];
  gtype_real32 volts[MAX_NUM_MEASUREMENTS];
  gtype_real32 calbMeasurements[MAX_NUM_MEASUREMENTS];
  gtype_real32 averageCalbMeasurement;
  char units[20];

  for (channel = NGIO_CHANNEL_ID_ANALOG1; channel <= NGIO_CHANNEL_ID_ANALOG4; channel++) {
    NGIO_Device_DDSMem_GetSensorNumber(hDevice, channel, &sensorId, 0, &sig, 0);
    if (sensorId != 0) {
      char longname[30];
      longname[0] = 0;
      fprintf(stderr, "Sensor id in channel ANALOG%d = %d", channel, sensorId);
      NGIO_Device_DDSMem_GetLongName(hDevice, channel, longname, sizeof(longname));
      if (strlen(longname) != 0) fprintf(stderr, " Sensor Name = '%s' ", longname);

      int probeType = NGIO_Device_GetProbeType(hDevice, channel);
      numMeasurements = NGIO_Device_ReadRawMeasurements(hDevice, channel, rawMeasurements, NULL, MAX_NUM_MEASUREMENTS);
      if (numMeasurements > 0) {
        averageCalbMeasurement = 0.0;
        for (i = 0; i < numMeasurements; i++) {
          volts[i] = NGIO_Device_ConvertToVoltage(hDevice, channel, rawMeasurements[i], probeType);
          calbMeasurements[i] = NGIO_Device_CalibrateData(hDevice, channel, volts[i]);
          averageCalbMeasurement += calbMeasurements[i];
        }
        if (numMeasurements > 1) {
          averageCalbMeasurement = averageCalbMeasurement/numMeasurements;
        }
        if (!strncmp("NH4 ISE", longname, 7)) {
          measurement->ammonium = averageCalbMeasurement;
        }

        gtype_real32 a, b, c;
        unsigned char activeCalPage = 0;
        NGIO_Device_DDSMem_GetActiveCalPage(hDevice, channel, &activeCalPage);
        NGIO_Device_DDSMem_GetCalPage(hDevice, channel, activeCalPage, &a, &b, &c, units, sizeof(units));            
        fprintf(stderr, "; average of %d measurements = %8.3f %s .", numMeasurements, averageCalbMeasurement, units);
      }
      fprintf(stderr, "\n");
    }
  }
}

void NGIO_StopMeasurements(NGIO_DEVICE_HANDLE hDevice)
{
	NGIOGetStatusCmdResponsePayload getStatusResponse;
	gtype_uint32 nRespBytes;
  gtype_int32 status = 0;

  NGIO_Device_SendCmdAndGetResponse(hDevice, NGIO_CMD_ID_STOP_MEASUREMENTS, NULL, 0, NULL, NULL, NGIO_TIMEOUT_MS_DEFAULT);
  memset(&getStatusResponse, 0, sizeof(getStatusResponse));
  nRespBytes = sizeof(getStatusResponse);
  status = NGIO_Device_SendCmdAndGetResponse(hDevice, NGIO_CMD_ID_GET_STATUS, NULL, 0, &getStatusResponse,
                                             &nRespBytes, NGIO_TIMEOUT_MS_DEFAULT);
  if (0 == status) {
    printf("DAQ reports status byte of %xh\n", getStatusResponse.status);
  }
}

int main(int argc, char* argv[])
{
  /* struct MHD_Daemon *daemon; */
  struct aqx_measurement measurement;
  int any_devices_connected = 0;
  gtype_uint32 ngio_deviceType = 0;
  int i;

  init_system();

  GOIO_SENSOR_HANDLE hTempDevice = GoIO_OpenTemperatureDevice();
  NGIO_DEVICE_HANDLE hLabQuestDevice = NGIO_OpenLabQuestDevices(&ngio_deviceType);

  any_devices_connected = hTempDevice != NULL || hLabQuestDevice != NULL;

  if (any_devices_connected) {

    OSSleep(1000); /* sync time just in case */

    for (i = 0; i < 1000; i ++) {
      if (hTempDevice) GoIO_SendIORequest(hTempDevice);
      if (hLabQuestDevice) NGIO_SendIORequest(hLabQuestDevice, ngio_deviceType);

      OSSleep(1000); /* wait for a second */

      if (hTempDevice) {
        measurement.temperature = GoIO_CollectMeasurement(hTempDevice);
      }

      if (hLabQuestDevice) {
        NGIO_CollectMeasurements(hLabQuestDevice, &measurement);
        NGIO_StopMeasurements(hLabQuestDevice);
      }
      /* Register time after all measurements were taken */
      time(&measurement.time);
      aqx_add_measurement(&measurement);
    }

    /* Make sure we did not lose any measurements */
    fprintf(stderr, "Submitting measurements...\n");
    aqx_client_flush();

  } else {
    fprintf(stderr, "no devices connected (%d), don't submit\n", any_devices_connected);
  }

  /* cleanup here */
  if (hTempDevice) GoIO_Sensor_Close(hTempDevice);
  if (hLabQuestDevice) NGIO_Device_Close(hTempDevice);

  cleanup_system();
  /*
  daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, HTTP_PORT, NULL, NULL,
                            &answer_to_connection, NULL, MHD_OPTION_END);
  if (daemon) {
    getchar();
    MHD_stop_daemon(daemon);
    }
  */
	return 0;
}

bool GoIO_GetAvailableDeviceName(char *deviceName, gtype_int32 nameLength, gtype_int32 *pVendorId, gtype_int32 *pProductId)
{
	bool bFoundDevice = false;
	deviceName[0] = 0;
	int numSkips = GoIO_UpdateListOfAvailableDevices(VERNIER_DEFAULT_VENDOR_ID, SKIP_DEFAULT_PRODUCT_ID);
	int numJonahs = GoIO_UpdateListOfAvailableDevices(VERNIER_DEFAULT_VENDOR_ID, USB_DIRECT_TEMP_DEFAULT_PRODUCT_ID);
	int numCyclopses = GoIO_UpdateListOfAvailableDevices(VERNIER_DEFAULT_VENDOR_ID, CYCLOPS_DEFAULT_PRODUCT_ID);
	int numMiniGCs = GoIO_UpdateListOfAvailableDevices(VERNIER_DEFAULT_VENDOR_ID, MINI_GC_DEFAULT_PRODUCT_ID);

	if (numSkips > 0)
	{
		GoIO_GetNthAvailableDeviceName(deviceName, nameLength, VERNIER_DEFAULT_VENDOR_ID, SKIP_DEFAULT_PRODUCT_ID, 0);
		*pVendorId = VERNIER_DEFAULT_VENDOR_ID;
		*pProductId = SKIP_DEFAULT_PRODUCT_ID;
		bFoundDevice = true;
	}
	else if (numJonahs > 0)
	{
		GoIO_GetNthAvailableDeviceName(deviceName, nameLength, VERNIER_DEFAULT_VENDOR_ID, USB_DIRECT_TEMP_DEFAULT_PRODUCT_ID, 0);
		*pVendorId = VERNIER_DEFAULT_VENDOR_ID;
		*pProductId = USB_DIRECT_TEMP_DEFAULT_PRODUCT_ID;
		bFoundDevice = true;
	}
	else if (numCyclopses > 0)
	{
		GoIO_GetNthAvailableDeviceName(deviceName, nameLength, VERNIER_DEFAULT_VENDOR_ID, CYCLOPS_DEFAULT_PRODUCT_ID, 0);
		*pVendorId = VERNIER_DEFAULT_VENDOR_ID;
		*pProductId = CYCLOPS_DEFAULT_PRODUCT_ID;
		bFoundDevice = true;
	}
	else if (numMiniGCs > 0)
	{
		GoIO_GetNthAvailableDeviceName(deviceName, nameLength, VERNIER_DEFAULT_VENDOR_ID, MINI_GC_DEFAULT_PRODUCT_ID, 0);
		*pVendorId = VERNIER_DEFAULT_VENDOR_ID;
		*pProductId = MINI_GC_DEFAULT_PRODUCT_ID;
		bFoundDevice = true;
	}

	return bFoundDevice;
}

void OSSleep(
	unsigned long msToSleep)//milliseconds
{
#ifdef TARGET_OS_WIN
	::Sleep(msToSleep);
#endif
#ifdef TARGET_OS_LINUX
  struct timeval tv;
  unsigned long usToSleep = msToSleep*1000;
  tv.tv_sec = usToSleep/1000000;
  tv.tv_usec = usToSleep % 1000000;
  select (0, NULL, NULL, NULL, &tv);
#endif
#ifdef TARGET_OS_MAC
	AbsoluteTime absTime = ::AddDurationToAbsolute(msToSleep * durationMillisecond, ::UpTime());
	::MPDelayUntil(&absTime);
#endif
}
