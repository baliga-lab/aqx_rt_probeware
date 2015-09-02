TESTS = test/alltests

AM_CFLAGS   = -g -Wall

AM_CPPFLAGS = -DTARGET_OS_LINUX $(GOIO_CFLAGS) $(NGIO_CFLAGS)

LOG_DRIVER = env AM_TAP_AWK='$(AWK)' $(SHELL) \
              $(top_srcdir)/tap-driver.sh

bin_PROGRAMS = aqxclient

aqxclient_SOURCES = 	aqxclient.cpp aqxapi_client.c simple_templates.c

aqxclient_LDADD   = $(GOIO_LIBS) $(NGIO_LIBS) -lmicrohttpd -lcurl -ljson-c

check_PROGRAMS = $(TESTS)

test_alltests_SOURCES = test/simple_templates_test.c test/chibi.c simple_templates.c
