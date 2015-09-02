/*
 * aqxclient.cpp : Defines the entry point for the console application.
 *
 * TODO:
 * - Handling of disconnection errors:
 *   At the moment disconnected devices are marked at such and excluded from
 *   measuring. We might want to reconsider reconnecting everything after
 *   a connection error happens
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <memory.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>

#ifdef TARGET_OS_WIN
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#pragma warning(disable: 4996)
#endif
#ifdef TARGET_OS_LINUX
#include <sys/time.h>
#define HAS_SIGACTION
#endif
#ifdef TARGET_OS_MAC
#include <Carbon/Carbon.h>
#define HAS_SIGACTION
#endif

extern "C" {
#include "aqxapi_client.h"
#include "simple_templates.h"
}

#include <microhttpd.h>
#define HTTP_PORT 8080

#define MAX_NUM_MEASUREMENTS 100

/* Remove or comment for production */
//#define DONT_SUBMIT_TO_API

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
  int connected;
};

struct ngio_device {
  NGIO_DEVICE_HANDLE hDevice;
  gtype_int32 deviceType;
  int connected;
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
int should_exit = 0;

void GoIO_OpenAllConnectedDevices();
void GoIO_CloseAllConnectedDevices();
void GoIO_SendIORequests();
void GoIO_CollectMeasurements(struct aqx_measurement *measurement);

void NGIO_OpenAllConnectedDevices(NGIO_LIBRARY_HANDLE hNGIOlib);
gtype_int32 NGIO_SendIORequest(struct ngio_device *device);
void NGIO_CollectMeasurements(NGIO_DEVICE_HANDLE hDevice, struct aqx_measurement *measurement);
void NGIO_StopMeasurements(NGIO_DEVICE_HANDLE hDevice);

static void OSSleep(unsigned long msToSleep);

void signal_handler(int signum)
{
  LOG_DEBUG("Signal: %d received\n", signum);
  should_exit = 1;
}

/*
 * Handle the HTTP server requests here
 */
int answer_to_connection (void *cls, struct MHD_Connection *connection,
                          const char *url,
                          const char *method, const char *version,
                          const char *upload_data,
                          size_t *upload_data_size, void **con_cls)
{
  struct MHD_Response *response;
  FILE *fp;
  int ret, fd;
  const char *filepath;

  LOG_DEBUG("Method: '%s', URL: '%s', Version: '%s' upload data: '%s'\n",
            method, url, version, upload_data);
  if (!strcmp(url, "/favicon.ico")) {
    filepath = "htdocs/favicon.ico";
  } else {
    filepath = "htdocs/index.html";
  }

  fp = fopen(filepath, "r");
  if (fp) {
    uint64_t size;
    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    rewind(fp);
    response = MHD_create_response_from_fd(size, fileno(fp));
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response); /* destroy will auto close the file */
    return ret;
  } else {
    LOG_DEBUG("open failed\n");
    return ret;
  }
}


struct aqxclient_config {
  char service_url[200];
  char system_uid[48];
  char refresh_token[100];
  int send_interval_secs;
};


void parse_config_line(struct aqxclient_config *cfg, char *line)
{
  /* remove trailing white space */
  int line_end = strlen(line);
  while (line_end > 0 && isspace(line[line_end - 1])) {
    line[line_end - 1] = 0;
    line_end--;
  }

  /* extract configuration settings */
  if (!strncmp(line, "service_url=", strlen("service_url="))) {
    strncpy(cfg->service_url, &line[strlen("service_url=")], sizeof(cfg->service_url));
  } else if (!strncmp(line, "system_uid=", strlen("system_uid="))) {
    strncpy(cfg->system_uid, &line[strlen("system_uid=")], sizeof(cfg->system_uid));
  } else if (!strncmp(line, "refresh_token=", strlen("refresh_token="))) {
    strncpy(cfg->refresh_token, &line[strlen("refresh_token=")], sizeof(cfg->refresh_token));
  } else if (!strncmp(line, "send_interval_secs=", strlen("send_interval_secs="))) {
    LOG_DEBUG("parsing int at: '%s'\n", &line[strlen("send_interval_secs=")]);
    cfg->send_interval_secs = atoi(&line[strlen("send_interval_secs=")]);
  }
}

static struct aqxclient_config *read_config()
{
  FILE *fp = fopen("config.ini", "r");
  static char line_buffer[200], *s;
  static struct aqxclient_config cfg;

  if (fp) {
    LOG_DEBUG("configuration file opened\n");
    while (fgets(line_buffer, sizeof(line_buffer), fp)) {
      parse_config_line(&cfg, line_buffer);
    }
    LOG_DEBUG("service url: '%s', system_uid: '%s', refresh_token: '%s', interval(secs): %d\n",
              cfg.service_url, cfg.system_uid, cfg.refresh_token, cfg.send_interval_secs);
    fclose(fp);
  }
  return &cfg;
}

NGIO_LIBRARY_HANDLE init_system()
{
	gtype_uint16 goio_minor, goio_major, ngio_minor, ngio_major;
  struct aqx_client_options aqx_options;
  NGIO_LIBRARY_HANDLE hNGIOlib;
  struct aqxclient_config *cfg = read_config();

  aqx_options.service_url = cfg->service_url;
  aqx_options.system_uid = cfg->system_uid;
  aqx_options.oauth2_refresh_token = cfg->refresh_token;
  aqx_options.send_interval_secs = cfg->send_interval_secs;

	GoIO_Init();
	GoIO_GetDLLVersion(&goio_major, &goio_minor);
	hNGIOlib = NGIO_Init();
	NGIO_GetDLLVersion(hNGIOlib, &ngio_major, &ngio_minor);

	LOG_DEBUG("aqx_client V0.001 - (c) 2015 Institute for Systems Biology\nGoIO library version %d.%d\nNGIO library version %d.%d\n",
          goio_major, goio_minor, ngio_major, ngio_minor);

  aqx_client_init(&aqx_options);
  return hNGIOlib;
}

void cleanup_system(NGIO_LIBRARY_HANDLE hNGIOlib)
{
  NGIO_Uninit(hNGIOlib);
	GoIO_Uninit();
  aqx_client_cleanup();
}

void measure()
{
  struct aqx_measurement measurement;
  int any_devices_connected = num_goio_devices > 0 || num_ngio_devices > 0;
  int i, j;
  gtype_int32 status;

  if (any_devices_connected) {
    GoIO_SendIORequests();
    for (j = 0; j < num_ngio_devices; j++) {
      if (ngio_devices[j].connected) status = NGIO_SendIORequest(&ngio_devices[j]);
    }

    OSSleep(1000); // wait for a second
    GoIO_CollectMeasurements(&measurement);

    for (j = 0; j < num_ngio_devices; j++) {
      if (ngio_devices[j].connected) {
        NGIO_CollectMeasurements(ngio_devices[j].hDevice, &measurement);
        NGIO_StopMeasurements(ngio_devices[j].hDevice);
      }
    }
#ifndef DONT_SUBMIT_TO_API
    // Register time after all measurements were taken
    time(&measurement.time);
    aqx_add_measurement(&measurement);
#endif
  } else {
    OSSleep(1000); // wait for a second
    LOG_DEBUG("no devices connected (%d), don't submit\n", any_devices_connected);
  }
}

int main(int argc, char* argv[])
{
  struct MHD_Daemon *daemon;
  struct stemp_dict *dict;
  int i;

#ifdef HAS_SIGACTION
  struct sigaction act;
  int retval;
#endif

  NGIO_LIBRARY_HANDLE hNGIOlib;

  hNGIOlib = init_system();

  // Open all connected GOIO devices
  GoIO_OpenAllConnectedDevices();
  NGIO_OpenAllConnectedDevices(hNGIOlib);


  LOG_DEBUG("starting the HTTP daemon...");
  daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, HTTP_PORT, NULL, NULL,
                            &answer_to_connection, NULL, MHD_OPTION_END);

  /*
    We install a simple signal handler so we can cleanly shutdown.
    INT for Ctrl-C and TERM for start-stop handler signals.

    Windows does not have sigaction, but sigaction is recommended, so
    we might have put an ifdef here
    TODO: Windows might have to add another stop handler for stop service
  */
#ifdef HAS_SIGACTION
  LOG_DEBUG("using sigaction() to install signal handler\n");
  memset(&act, 0, sizeof(struct sigaction));
  act.sa_flags = SA_SIGINFO;
  act.sa_handler = signal_handler;
  retval = sigaction(SIGTERM, &act, NULL);
  retval = sigaction(SIGINT, &act, NULL);
#else
  // Windows supports signal in a very incomplete way
  LOG_DEBUG("using signal() to install signal handler\n");
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
#endif

  if (daemon) {
    LOG_DEBUG("HTTP daemon started...");
    OSSleep(1000); // sync time just in case

    while (!should_exit) {
      measure();
    }

    // Make sure we did not lose any measurements
#ifndef DONT_SUBMIT_TO_API
    LOG_DEBUG("Submitting final measurements...\n");
    aqx_client_flush();
#endif

    LOG_DEBUG("HTTP daemon stopped...");
    MHD_stop_daemon(daemon);
  }

  /* cleanup here */
  GoIO_CloseAllConnectedDevices();
  for (i = 0; i < num_ngio_devices; i++) {
    if (ngio_devices[i].hDevice) NGIO_Device_Close(ngio_devices[i].hDevice);
  }

  cleanup_system(hNGIOlib);
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
      device->connected = 1;
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
  LOG_DEBUG("# of connected GoIO devices: %d\n", num_goio_devices);
}

void GoIO_CloseAllConnectedDevices()
{
  int i;
  for (i = 0; i < num_goio_devices; i++) {
    if (goio_devices[i].hDevice) GoIO_Sensor_Close(goio_devices[i].hDevice);
  }
}

/*
  Step 1: Send IO request
  This will return 0 on success, and -1 on error
*/
gtype_int32 GoIO_SendIORequest(GOIO_SENSOR_HANDLE hDevice)
{
  GoIO_Sensor_SetMeasurementPeriod(hDevice, 0.040, SKIP_TIMEOUT_MS_DEFAULT); //40 milliseconds measurement period.
  return GoIO_Sensor_SendCmdAndGetResponse(hDevice, SKIP_CMD_ID_START_MEASUREMENTS, NULL, 0, NULL, NULL, SKIP_TIMEOUT_MS_DEFAULT);
}

void GoIO_SendIORequests()
{
  gtype_int32 retval;
  unsigned char lastCmd, lastCmdStatus, lastCmdWithErrorRespSentOvertheWire, lastErrorSentOvertheWire;
  int i;
  for (i = 0; i < num_goio_devices; i++) {
    if (goio_devices[i].connected) {
      retval = GoIO_SendIORequest(goio_devices[i].hDevice);
      if (retval) {
        retval = GoIO_Sensor_GetLastCmdResponseStatus(goio_devices[i].hDevice,
                                                      &lastCmd, &lastCmdStatus,
                                                      &lastCmdWithErrorRespSentOvertheWire,
                                                      &lastErrorSentOvertheWire);
        /* there was an error !!!!
           For now, we mark the sensor as disconnected and ignore measurements from there */
        LOG_DEBUG("connection error, disconnected GOIO: '%s'\n", goio_devices[i].description);
        goio_devices[i].connected = 0;
        GoIO_Sensor_Close(goio_devices[i].hDevice);
        goio_devices[i].hDevice = NULL;
      }
    }
  }
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
    if (goio_devices[i].connected) {
      double value = GoIO_CollectMeasurement(goio_devices[i].hDevice);
      LOG_DEBUG("sensor '%s' = %f\n", goio_devices[i].description, value);
      if (IS_PH(goio_devices[i].description)) measurement->ph = value;
      else if (IS_NH4(goio_devices[i].description)) measurement->ammonium = value;
      else if (IS_NO3(goio_devices[i].description)) measurement->nitrate = value;
      else if (IS_DO(goio_devices[i].description)) measurement->o2 = value;
      else if (IS_TEMPERATURE(goio_devices[i].description)) measurement->temperature = value;
      else LOG_DEBUG("sensor '%s' not yet supported for GoIO\n", goio_devices[i].description);
    }
  }
}

/**********************************************************************
 * NGIO device interaction
 **********************************************************************/

void NGIO_OpenConnectedDevicesOfType(NGIO_LIBRARY_HANDLE hNGIOlib, gtype_uint32 deviceType)
{
	char deviceName[NGIO_MAX_SIZE_DEVICE_NAME];
	NGIO_DEVICE_LIST_HANDLE hDeviceList;
	gtype_uint32 sig, mask, numDevices;
	gtype_int32 status = 0;
  struct ngio_device *device;
  int i;

  NGIO_SearchForDevices(hNGIOlib, deviceType, NGIO_COMM_TRANSPORT_USB, NULL, &sig);  
  hDeviceList = NGIO_OpenDeviceListSnapshot(hNGIOlib, deviceType, &numDevices, &sig);
  LOG_DEBUG("# devices: %d\n", numDevices);
  for (i = 0; i < numDevices; i++) {
    device = &ngio_devices[num_ngio_devices];
    device->deviceType = deviceType;
    status = NGIO_DeviceListSnapshot_GetNthEntry(hDeviceList, i,
                                                 deviceName, sizeof(deviceName),
                                                 &mask);
    if (!status) {
      device->hDevice = NGIO_Device_Open(hNGIOlib, deviceName, 0);
      if (device->hDevice) {
        LOG_DEBUG("successfully opened NGIO device, type: %d\n", device->deviceType);
        device->connected = 1;
        num_ngio_devices++;
      }
    }
  }
  NGIO_CloseDeviceListSnapshot(hDeviceList);
}

void NGIO_OpenAllConnectedDevices(NGIO_LIBRARY_HANDLE hNGIOlib)
{  
  LOG_DEBUG("searching for LabQuest devices\n");
  NGIO_OpenConnectedDevicesOfType(hNGIOlib, NGIO_DEVTYPE_LABQUEST);
  LOG_DEBUG("searching for LabQuest Mini devices\n");
  NGIO_OpenConnectedDevicesOfType(hNGIOlib, NGIO_DEVTYPE_LABQUEST_MINI);
  LOG_DEBUG("searching for LabQuest 2 devices\n");
  NGIO_OpenConnectedDevicesOfType(hNGIOlib, NGIO_DEVTYPE_LABQUEST2);
  LOG_DEBUG("# NGIO devices: %d\n", num_ngio_devices);
}

gtype_int32 NGIO_SendIORequest(struct ngio_device *device)
{
	NGIOGetStatusCmdResponsePayload getStatusResponse;
	NGIO_NVMEM_CHANNEL_ID1_rec getNVMemResponse;
	gtype_uint32 nRespBytes;
  gtype_int32 status = 0, deviceType = device->deviceType;
  NGIO_DEVICE_HANDLE hDevice = device->hDevice;

  if ((NGIO_DEVTYPE_LABQUEST == deviceType) || (NGIO_DEVTYPE_LABQUEST2 == deviceType)) {
#if !(defined(TARGET_PLATFORM_LABQUEST) || defined(TARGET_PLATFORM_LABQUEST2))
    /* Wrest control of the LabQuest data acquisition subsystem(the DAQ) away from the GUI app running
       down on the LabQuest. */
    status = NGIO_Device_AcquireExclusiveOwnership(hDevice, NGIO_GRAB_DAQ_TIMEOUT);
    if (status) {
      LOG_DEBUG("NGIO_Device_AcquireExclusiveOwnership() failed!\n");
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
    LOG_DEBUG("LabQuest serial number(yy ww nnnnnnnn) is %02x %02x %08d\n", 
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
  if (status) {
    LOG_DEBUG("connection error, disconnected NGIO device\n");
    device->connected = 0;
    NGIO_Device_Close(device->hDevice);
    device->hDevice = NULL;
  }
  return status;
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
          LOG_DEBUG("Unsupported Sensor detected in ANALOG%d = %d ('%s')\n",
                    channel, sensorId, longname);
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
          LOG_DEBUG("Sensor id in channel ANALOG%d = %d ('%s')", channel, sensorId, longname);
          LOG_DEBUG("; average of %d measurements = %8.3f %s\n",
                    numMeasurements, averageCalbMeasurement, units);
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
