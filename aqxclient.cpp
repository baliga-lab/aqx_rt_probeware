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

/* Remove or comment for production */
#define DONT_SUBMIT_TO_API

/* Please replace for user */
#define REFRESH_TOKEN "1/uHlxK48dCAolwIS-FckPhaMcWMKrdO7QVbo9E_Kb_k1IgOrJDtdun6zK6XiATCKT"
#define SYSTEM_UID "7921a6763e0011e5beb064273763ec8b"
#define SEND_INTERVAL_SECS 60

/* TODO: This is actually a feature of the measuring component */
#define SECONDS_PER_SAMPLE 1

#include "GoIO_DLL_interface.h"
#include "NGIO_lib_interface.h"

#define SENSOR_ID_PH     20
#define SENSOR_ID_DO1    37
#define SENSOR_ID_NH4    39
#define SENSOR_ID_NO3    40
#define SENSOR_ID_TEMP   60
#define SENSOR_ID_DO2    99

#define SENSOR_NAME_PH   "PH"
#define SENSOR_NAME_NH4  "NH4 ISE"
#define SENSOR_NAME_NO3  "NO3 ISE"
#define SENSOR_NAME_TEMP "Temperature"
#define SENSOR_NAME_DO1  "D. Oxygen"
#define SENSOR_NAME_DO2  "D. OXYGEN"

#define MAX_CONNECTED_GOIO_DEVICES 8
#define MAX_CONNECTED_NGIO_DEVICES 4

struct goio_device {
  GOIO_SENSOR_HANDLE hDevice;
  char description[100];
};

struct ngio_device {
  NGIO_DEVICE_HANDLE hDevice;
  gtype_int32 deviceType;
};

int num_goio_devices;
struct goio_device goio_devices[MAX_CONNECTED_GOIO_DEVICES];

int num_ngio_devices = 0;
struct ngio_device ngio_devices[MAX_CONNECTED_NGIO_DEVICES];

#define IS_TEMPERATURE(devname) (devname && !strncmp(SENSOR_NAME_TEMP, devname, sizeof(devname)))

#define IS_PH(devname)  (devname && !strncmp(SENSOR_NAME_PH, devname, sizeof(devname)))
#define IS_NH4(devname) (devname && !strncmp(SENSOR_NAME_NH4, devname, sizeof(devname)))
#define IS_NO3(devname) (devname && !strncmp(SENSOR_NAME_NO3, devname, sizeof(devname)))
#define IS_DO(devname)  (devname && (!strncmp(SENSOR_NAME_DO1, devname, sizeof(devname)) || !strncmp(SENSOR_NAME_DO2, devname, sizeof(devname)) ))

const char *goio_deviceDesc[8] = {"?", "?", "Go! Temp", "Go! Link", "Go! Motion", "?", "?", "Mini GC"};

NGIO_LIBRARY_HANDLE g_hNGIOlib = NULL;

void GoIO_OpenAllConnectedDevices();
void GoIO_CloseAllConnectedDevices();
void GoIO_SendIORequests();
void GoIO_CollectMeasurements(struct aqx_measurement *measurement);

NGIO_DEVICE_HANDLE NGIO_OpenLabQuestDevices(gtype_uint32 *retDeviceType);
void NGIO_SendIORequest(NGIO_DEVICE_HANDLE hDevice, gtype_uint32 deviceType);
void NGIO_CollectMeasurements(NGIO_DEVICE_HANDLE hDevice, struct aqx_measurement *measurement);
void NGIO_StopMeasurements(NGIO_DEVICE_HANDLE hDevice);

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


int main(int argc, char* argv[])
{
  /* struct MHD_Daemon *daemon; */
  struct aqx_measurement measurement;
  int any_devices_connected = 0;
  gtype_uint32 ngio_deviceType = 0;
  int i;

  init_system();

  // Open all connected GOIO devices
  GoIO_OpenAllConnectedDevices();
  //NGIO_OpenAllConnectedDevices();

  NGIO_DEVICE_HANDLE hLabQuestDevice = NGIO_OpenLabQuestDevices(&ngio_deviceType);

  any_devices_connected = num_goio_devices > 0 || hLabQuestDevice != NULL;

  if (any_devices_connected) {

    OSSleep(1000); // sync time just in case

    for (i = 0; i < 1000; i ++) {
      GoIO_SendIORequests();
      if (hLabQuestDevice) NGIO_SendIORequest(hLabQuestDevice, ngio_deviceType);

      OSSleep(1000); // wait for a second
      GoIO_CollectMeasurements(&measurement);

      if (hLabQuestDevice) {
        NGIO_CollectMeasurements(hLabQuestDevice, &measurement);
        NGIO_StopMeasurements(hLabQuestDevice);
      }
#ifndef DONT_SUBMIT_TO_API
      // Register time after all measurements were taken
      time(&measurement.time);
      aqx_add_measurement(&measurement);
#endif
    }

    // Make sure we did not lose any measurements
#ifndef DONT_SUBMIT_TO_API
    fprintf(stderr, "Submitting measurements...\n");
    aqx_client_flush();
#endif
  } else {
    fprintf(stderr, "no devices connected (%d), don't submit\n", any_devices_connected);
  }

  /* cleanup here */
  GoIO_CloseAllConnectedDevices();
  if (hLabQuestDevice) NGIO_Device_Close(hLabQuestDevice);

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

/**********************************************************************
 * GoIO device interaction
 **********************************************************************/
void GoIO_GetConnectedDevices(gtype_int32 productId)
{
  int numDevices;
	gtype_int32 vendorId;
  gtype_int32 i;
  struct goio_device *device = &goio_devices[num_goio_devices];
	char deviceName[GOIO_MAX_SIZE_DEVICE_NAME];

  numDevices = GoIO_UpdateListOfAvailableDevices(VERNIER_DEFAULT_VENDOR_ID, productId);
  for (i = 0; i < numDevices; i++) {
		GoIO_GetNthAvailableDeviceName(deviceName, GOIO_MAX_SIZE_DEVICE_NAME,
                                   VERNIER_DEFAULT_VENDOR_ID, productId, i);
		vendorId = VERNIER_DEFAULT_VENDOR_ID;
    device->hDevice = GoIO_Sensor_Open(deviceName, VERNIER_DEFAULT_VENDOR_ID,
                                       productId, 0);
    if (device->hDevice) {
      unsigned char charId;

      // Phase 1 send io request
      GoIO_Sensor_DDSMem_GetSensorNumber(device->hDevice, &charId, 0, 0);
      GoIO_Sensor_DDSMem_GetLongName(device->hDevice, device->description,
                                     sizeof(device->description));

      printf("Successfully opened '%s' device '%s' (%s), sensor %d\n", goio_deviceDesc[productId],
             deviceName, device->description, charId);
      num_goio_devices++;
    }
	}
}

void GoIO_OpenAllConnectedDevices()
{
  GoIO_GetConnectedDevices(SKIP_DEFAULT_PRODUCT_ID);
  GoIO_GetConnectedDevices(USB_DIRECT_TEMP_DEFAULT_PRODUCT_ID);
  GoIO_GetConnectedDevices(CYCLOPS_DEFAULT_PRODUCT_ID);
  GoIO_GetConnectedDevices(MINI_GC_DEFAULT_PRODUCT_ID);
  fprintf(stderr, "# of connected GoIO devices: %d\n", num_goio_devices);
}

void GoIO_CloseAllConnectedDevices()
{
  int i;
  for (i = 0; i < num_goio_devices; i++) {
    GoIO_Sensor_Close(goio_devices[i].hDevice);
  }
}

/* Step 1: Send IO request */
void GoIO_SendIORequest(GOIO_SENSOR_HANDLE hDevice)
{
  GoIO_Sensor_SetMeasurementPeriod(hDevice, 0.040, SKIP_TIMEOUT_MS_DEFAULT);//40 milliseconds measurement period.
  GoIO_Sensor_SendCmdAndGetResponse(hDevice, SKIP_CMD_ID_START_MEASUREMENTS, NULL, 0, NULL, NULL, SKIP_TIMEOUT_MS_DEFAULT);
}

void GoIO_SendIORequests()
{
  int i;
  for (i = 0; i < num_goio_devices; i++) GoIO_SendIORequest(goio_devices[i].hDevice);
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

void GoIO_CollectMeasurements(struct aqx_measurement *measurement)
{
  int i;
  for (i = 0; i < num_goio_devices; i++) {
    double value = GoIO_CollectMeasurement(goio_devices[i].hDevice);
    fprintf(stderr, "sensor '%s' = %f\n", goio_devices[i].description, value);
    if (IS_PH(goio_devices[i].description)) measurement->ph = value;
    else if (IS_NH4(goio_devices[i].description)) measurement->ammonium = value;
    else if (IS_NO3(goio_devices[i].description)) measurement->nitrate = value;
    else if (IS_DO(goio_devices[i].description)) measurement->o2 = value;
    else if (IS_TEMPERATURE(goio_devices[i].description)) measurement->temperature = value;
    else fprintf(stderr, "sensor '%s' not yet supported for GoIO\n", goio_devices[i].description);
  }
}

/**********************************************************************
 * NGIO device interaction
 **********************************************************************/

void NGIO_OpenConnectedDevicesOfType(gtype_uint32 deviceType)
{
	char deviceName[NGIO_MAX_SIZE_DEVICE_NAME];
	NGIO_DEVICE_LIST_HANDLE hDeviceList;
	gtype_uint32 sig, mask, numDevices;
	gtype_int32 status = 0;
  struct ngio_device *device;
  int i;

  NGIO_SearchForDevices(g_hNGIOlib, deviceType, NGIO_COMM_TRANSPORT_USB, NULL, &sig);  
  hDeviceList = NGIO_OpenDeviceListSnapshot(g_hNGIOlib, deviceType, &numDevices, &sig);
  fprintf(stderr, "# devices: %d\n", numDevices);
  for (int i = 0; i < numDevices; i++) {
    device = &ngio_devices[num_ngio_devices];
    device->deviceType = deviceType;
    status = NGIO_DeviceListSnapshot_GetNthEntry(hDeviceList, i,
                                                 deviceName, sizeof(deviceName),
                                                 &mask);
    if (!status) {
      device->hDevice = NGIO_Device_Open(g_hNGIOlib, deviceName, 0);
      if (device->hDevice) {
        fprintf(stderr, "successfully opened NGIO device, type: %d, handle: %d\n",
                device->deviceType, device->hDevice);
        num_ngio_devices++;
      }
    }
  }
  NGIO_CloseDeviceListSnapshot(hDeviceList);
}

void NGIO_OpenAllConnectedDevices()
{  
  fprintf(stderr, "searching for LabQuest devices\n");
  NGIO_OpenConnectedDevicesOfType(NGIO_DEVTYPE_LABQUEST);
  fprintf(stderr, "searching for LabQuest Mini devices\n");
  NGIO_OpenConnectedDevicesOfType(NGIO_DEVTYPE_LABQUEST_MINI);
  fprintf(stderr, "searching for LabQuest 2 devices\n");
  NGIO_OpenConnectedDevicesOfType(NGIO_DEVTYPE_LABQUEST2);
  fprintf(stderr, "# NGIO devices: %d\n", num_ngio_devices);
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

  if (!status) {
    fprintf(stderr, "NGIO LabQuest detected\n");
  }

  if (status) {
    deviceType = NGIO_DEVTYPE_LABQUEST_MINI;
    NGIO_SearchForDevices(g_hNGIOlib, deviceType, NGIO_COMM_TRANSPORT_USB, NULL, &sig);

    hDeviceList = NGIO_OpenDeviceListSnapshot(g_hNGIOlib, deviceType, &numDevices, &sig);
    status = NGIO_DeviceListSnapshot_GetNthEntry(hDeviceList, 0, deviceName, sizeof(deviceName), &mask);
    NGIO_CloseDeviceListSnapshot(hDeviceList);
    if (!status) {
      fprintf(stderr, "NGIO LabQuest Mini detected\n");
    }
  }
  if (status) {
    deviceType = NGIO_DEVTYPE_LABQUEST2;
    NGIO_SearchForDevices(g_hNGIOlib, deviceType, NGIO_COMM_TRANSPORT_USB, NULL, &sig);

    hDeviceList = NGIO_OpenDeviceListSnapshot(g_hNGIOlib, deviceType, &numDevices, &sig);
    status = NGIO_DeviceListSnapshot_GetNthEntry(hDeviceList, 0, deviceName, sizeof(deviceName), &mask);
    NGIO_CloseDeviceListSnapshot(hDeviceList);
    if (!status) {
      fprintf(stderr, "NGIO LabQuest2 detected\n");
    }
  }

  if (!status) {
    retval = NGIO_Device_Open(g_hNGIOlib, deviceName, 0);
    *retDeviceType = deviceType;
    if (retval) {
      fprintf(stderr, "NGIO Device Type: %d opened successfully\n", deviceType);
    } else {
      fprintf(stderr, "Opening NGIO Device failed !\n");
    }
  } else {
    fprintf(stderr, "no LabQuest connected devices detected.\n");
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
#ifdef DEBUG
    fprintf(stderr, "LabQuest serial number(yy ww nnnnnnnn) is %02x %02x %08d\n", 
           (gtype_uint16) getNVMemResponse.serialNumber.yy, 
           (gtype_uint16) getNVMemResponse.serialNumber.ww, serialNum);
#endif
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
  char longname[30];
  int is_supported_sensor;

  for (channel = NGIO_CHANNEL_ID_ANALOG1; channel <= NGIO_CHANNEL_ID_ANALOG4; channel++) {
    is_supported_sensor = 1;
    NGIO_Device_DDSMem_GetSensorNumber(hDevice, channel, &sensorId, 0, &sig, 0);
    if (sensorId != 0) {
      longname[0] = 0;
      NGIO_Device_DDSMem_GetLongName(hDevice, channel, longname, sizeof(longname));

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
        
        if (IS_PH(longname)) measurement->ph = averageCalbMeasurement;
        else if (IS_NH4(longname)) measurement->ammonium = averageCalbMeasurement;
        else if (IS_NO3(longname)) measurement->nitrate = averageCalbMeasurement;
        else if (IS_DO(longname)) measurement->o2 = averageCalbMeasurement;
        else {
          fprintf(stderr, "Unsupported Sensor detected in ANALOG%d = %d ('%s')\n", channel, sensorId, longname);
          is_supported_sensor = 0;
        }

        /*
          TODO: note that the DO sensor can measure either "mg/l" or "PCT"
         */
        if (is_supported_sensor) {
          gtype_real32 a, b, c;
          unsigned char activeCalPage = 0;
          NGIO_Device_DDSMem_GetActiveCalPage(hDevice, channel, &activeCalPage);
          NGIO_Device_DDSMem_GetCalPage(hDevice, channel, activeCalPage, &a, &b, &c, units, sizeof(units));
          fprintf(stderr, "Sensor id in channel ANALOG%d = %d ('%s')", channel, sensorId, longname);
          fprintf(stderr, "; average of %d measurements = %8.3f %s\n", numMeasurements, averageCalbMeasurement, units);
        }
      }
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
