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

const char *deviceDesc[8] = {"?", "?", "Go! Temp", "Go! Link", "Go! Motion", "?", "?", "Mini GC"};

bool GetAvailableDeviceName(char *deviceName, gtype_int32 nameLength, gtype_int32 *pVendorId, gtype_int32 *pProductId);
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

int main(int argc, char* argv[])
{
	char deviceName[GOIO_MAX_SIZE_DEVICE_NAME];
	gtype_int32 vendorId;		//USB vendor id
	gtype_int32 productId;		//USB product id
	char tmpstring[100];
	gtype_uint16 MajorVersion;
	gtype_uint16 MinorVersion;
	char units[20];
	char equationType = 0;

	gtype_int32 rawMeasurements[MAX_NUM_MEASUREMENTS];
	gtype_real64 volts[MAX_NUM_MEASUREMENTS];
	gtype_real64 calbMeasurements[MAX_NUM_MEASUREMENTS];
	gtype_int32 numMeasurements, i;
	gtype_real64 averageCalbMeasurement;

  struct MHD_Daemon *daemon;
  struct aqx_client_options aqx_options = {SYSTEM_UID, REFRESH_TOKEN, SEND_INTERVAL_SECS};
  struct aqx_measurement measurement;

	printf("aqxclient 0.1\n");
	GoIO_Init();
	GoIO_GetDLLVersion(&MajorVersion, &MinorVersion);
	printf("This app is linked to GoIO lib version %d.%d .\n", MajorVersion, MinorVersion);

  aqx_client_init(&aqx_options);

	bool bFoundDevice = GetAvailableDeviceName(deviceName, GOIO_MAX_SIZE_DEVICE_NAME, &vendorId, &productId);
	if (!bFoundDevice)
		printf("No Go devices found.\n");
	else
	{
		GOIO_SENSOR_HANDLE hDevice = GoIO_Sensor_Open(deviceName, vendorId, productId, 0);
		if (hDevice != NULL)
		{
			printf("Successfully opened %s device %s .\n", deviceDesc[productId], deviceName);

			unsigned char charId;
			GoIO_Sensor_DDSMem_GetSensorNumber(hDevice, &charId, 0, 0);
			printf("Sensor id = %d", charId);

			GoIO_Sensor_DDSMem_GetLongName(hDevice, tmpstring, sizeof(tmpstring));
			if (strlen(tmpstring) != 0)
				printf("(%s)", tmpstring);
			printf("\n");

			GoIO_Sensor_SetMeasurementPeriod(hDevice, 0.040, SKIP_TIMEOUT_MS_DEFAULT);//40 milliseconds measurement period.
			GoIO_Sensor_SendCmdAndGetResponse(hDevice, SKIP_CMD_ID_START_MEASUREMENTS, NULL, 0, NULL, NULL, SKIP_TIMEOUT_MS_DEFAULT);
			OSSleep(1000); //Wait 1 second.

			numMeasurements = GoIO_Sensor_ReadRawMeasurements(hDevice, rawMeasurements, MAX_NUM_MEASUREMENTS);
			printf("%d measurements received after about 1 second.\n", numMeasurements);
			averageCalbMeasurement = 0.0;
			for (i = 0; i < numMeasurements; i++)
			{
				volts[i] = GoIO_Sensor_ConvertToVoltage(hDevice, rawMeasurements[i]);
				calbMeasurements[i] = GoIO_Sensor_CalibrateData(hDevice, volts[i]);
				averageCalbMeasurement += calbMeasurements[i];
			}
			if (numMeasurements > 1)
				averageCalbMeasurement = averageCalbMeasurement/numMeasurements;

			GoIO_Sensor_DDSMem_GetCalibrationEquation(hDevice, &equationType);
			gtype_real32 a, b, c;
			unsigned char activeCalPage = 0;
			GoIO_Sensor_DDSMem_GetActiveCalPage(hDevice, &activeCalPage);
			GoIO_Sensor_DDSMem_GetCalPage(hDevice, activeCalPage, &a, &b, &c, units, sizeof(units));
			printf("Average measurement = %8.3f %s .\n", averageCalbMeasurement, units);

      /* set measurement */
      time(&measurement.time);
      measurement.temperature = averageCalbMeasurement;
      aqx_add_measurement(&measurement);
      aqx_client_flush();

			GoIO_Sensor_Close(hDevice);
		}
	}
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

bool GetAvailableDeviceName(char *deviceName, gtype_int32 nameLength, gtype_int32 *pVendorId, gtype_int32 *pProductId)
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
