
AM_CFLAGS   = -g -Wall

AM_CPPFLAGS = -DTARGET_OS_LINUX $(GOIO_CFLAGS)

bin_PROGRAMS = aqxclient

aqxclient_SOURCES = 	aqxclient.cpp aqxapi_client.c

aqxclient_LDADD   = $(GOIO_LIBS) -lmicrohttpd -lcurl -ljson-c

