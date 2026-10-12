#ifndef CONFIG_DEBUG_H
#define CONFIG_DEBUG_H

/**
 * Include example scene
 * in the build
 */
#define INCLUDE_EXAMPLE_SCENE true
// Include the test scenes
#define INCLUDE_TEST_SCENES true

#if DEBUG_FEATURES
    #define DEBUG_BUFFER_SIZE 0x100 // in bytes
#else
    #define DEBUG_BUFFER_SIZE 0x10
#endif

#endif
