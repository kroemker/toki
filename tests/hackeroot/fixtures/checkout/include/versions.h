#ifndef VERSIONS_H
#define VERSIONS_H

#define NTSC_1_2 4
#define GC_EU_MQ_DBG 10

#if OOT_VERSION == GC_EU_MQ_DBG && DEBUG_FEATURES
#define DEBUG_ASSETS 1
#else
#define DEBUG_ASSETS 0
#endif

#endif
