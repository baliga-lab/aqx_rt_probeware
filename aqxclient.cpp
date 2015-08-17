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
#define SEND_INTERVAL_SECS 10

/* TODO: This is actually a feature of the measuring component */
#define SECONDS_PER_SAMPLE 1

#include "GoIO_DLL_interface.h"
#include "NGIO_lib_interface.h"

const char *goio_deviceDesc[8] = {"?", "?", "Go! Temp", "Go! Link", "Go! Motion", "?", "?", "Mini GC"};

NGIO_LIBRARY_HANDLE g_hNGIOlib = NULL;
NGIO_DEVICE_HANDLE g_hDevice = NULL;

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


int main(int argc, char* argv[])
{
  struct MHD_Daemon *daemon;
  struct aqx_measurement measurement;
  int any_devices_connected = 0;

  init_system();

  GOIO_SENSOR_HANDLE hTempDevice = GoIO_OpenTemperatureDevice();
  any_devices_connected = hTempDevice != NULL;

  if (hTempDevice) {
    GoIO_SendIORequest(hTempDevice);
  }

  OSSleep(1000); /* wait for a second */

  if (hTempDevice) {
    measurement.temperature = GoIO_CollectMeasurement(hTempDevice);
  }

  if (any_devices_connected) {
    fprintf(stderr, "Submitting measurements...\n");
    /* Register time after all measurements were taken */
    time(&measurement.time);
    aqx_add_measurement(&measurement);
    aqx_client_flush();
	} else {
    fprintf(stderr, "no devices connected (%d), don't submit\n", any_devices_connected);
  }

  /* cleanup here */
  if (hTempDevice) GoIO_Sensor_Close(hTempDevice);

	GoIO_Uninit();
  aqx_client_cleanup();
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
