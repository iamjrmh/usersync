#define MINIAUDIO_IMPLEMENTATION
/* Only used for device I/O (tap-sync playback) here -- no whisper decode
 * path in this fork, but leave the generation API off since it's unused. */
#define MA_NO_GENERATION
#include "miniaudio.h"
