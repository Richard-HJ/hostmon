bin_PROGRAMS = hostmon_mon  

ARCH=@ARCH@
HJ_LOCATION=@HJ_LOCATION@

hostmon_mon_CPPFLAGS = -D$(ARCH) -Wall


INCLUDES = -I$(HJ_LOCATION)/include/libhj

hostmon_mon_SOURCES  = hostmon_mon.c net_test.h 


EXTRA_DIST = hostmon.spec.in hostmon.spec bootstrap README 