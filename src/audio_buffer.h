#ifndef MINIREND_AUDIO_BUFFER_H
#define MINIREND_AUDIO_BUFFER_H

#include <stddef.h>
#include <stdint.h>

#include "audio_engine.h"

/* Decode a RIFF/WAVE buffer into a MinirendAudioBuffer.
 * Returns NULL on error/unsupported format. */
MinirendAudioBuffer* minirend_audio_decode_wav(const uint8_t* data, size_t size);

#endif /* MINIREND_AUDIO_BUFFER_H */


