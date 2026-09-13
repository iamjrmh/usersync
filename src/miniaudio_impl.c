#define MINIAUDIO_IMPLEMENTATION
/* We use both decoding (for whisper) and device I/O (for the sync preview
 * audio player), so leave both subsystems enabled. The generation API
 * (procedural waveforms) is still unused. */
#define MA_NO_GENERATION
#include "miniaudio.h"
