#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "audio_buffer.h"

static uint16_t rd_u16le(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd_u32le(const uint8_t* p) {
    return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static int32_t sign_extend_24(uint32_t x) {
    if (x & 0x00800000u) {
        return (int32_t)(x | 0xFF000000u);
    }
    return (int32_t)x;
}

MinirendAudioBuffer* minirend_audio_decode_wav(const uint8_t* data, size_t size) {
    if (!data || size < 12) return NULL;
    if (memcmp(data, "RIFF", 4) != 0) return NULL;
    if (memcmp(data + 8, "WAVE", 4) != 0) return NULL;

    /* Walk chunks */
    uint16_t fmt_tag = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    const uint8_t* pcm = NULL;
    size_t pcm_bytes = 0;

    size_t off = 12;
    while (off + 8 <= size) {
        const uint8_t* ck = data + off;
        uint32_t ck_size = rd_u32le(ck + 4);
        const uint8_t* ck_data = ck + 8;
        if (off + 8 + (size_t)ck_size > size) break;

        if (memcmp(ck, "fmt ", 4) == 0) {
            if (ck_size < 16) return NULL;
            fmt_tag = rd_u16le(ck_data + 0);
            channels = rd_u16le(ck_data + 2);
            sample_rate = rd_u32le(ck_data + 4);
            bits_per_sample = rd_u16le(ck_data + 14);
            /* Handle extensible by treating it as PCM/float based on bits (best-effort). */
            if (fmt_tag == 0xFFFE && ck_size >= 40) {
                /* subformat GUID starts at byte 24, first 2 bytes are tag */
                fmt_tag = rd_u16le(ck_data + 24);
            }
        } else if (memcmp(ck, "data", 4) == 0) {
            pcm = ck_data;
            pcm_bytes = ck_size;
        }

        /* Chunks are word-aligned */
        off += 8 + (size_t)ck_size;
        if (off & 1) off++;
    }

    if (!pcm || pcm_bytes == 0) return NULL;
    if (channels == 0 || channels > 8) return NULL;
    if (sample_rate == 0) return NULL;
    if (bits_per_sample == 0) return NULL;

    int bytes_per_sample = (int)((bits_per_sample + 7) / 8);
    if (bytes_per_sample <= 0) return NULL;
    size_t frame_bytes = (size_t)bytes_per_sample * (size_t)channels;
    if (frame_bytes == 0) return NULL;
    int frames = (int)(pcm_bytes / frame_bytes);
    if (frames <= 0) return NULL;

    /* supported: PCM (1) and IEEE float (3) */
    if (!(fmt_tag == 1 || fmt_tag == 3)) return NULL;
    if (fmt_tag == 3 && bits_per_sample != 32) return NULL;

    MinirendAudioBuffer* out = minirend_audio_buffer_create((int)channels, (int)sample_rate, frames);
    if (!out) return NULL;
    float* dst = minirend_audio_buffer_data(out);
    if (!dst) {
        minirend_audio_buffer_destroy(out);
        return NULL;
    }

    for (int i = 0; i < frames; i++) {
        for (int c = 0; c < (int)channels; c++) {
            const uint8_t* sp = pcm + (size_t)i * frame_bytes + (size_t)c * (size_t)bytes_per_sample;
            float v = 0.0f;
            if (fmt_tag == 3) {
                /* float32 */
                uint32_t u = rd_u32le(sp);
                float f;
                memcpy(&f, &u, sizeof(float));
                v = f;
            } else {
                /* PCM */
                switch (bits_per_sample) {
                    case 8: {
                        uint8_t u = sp[0];
                        v = ((float)u - 128.0f) / 128.0f;
                        break;
                    }
                    case 16: {
                        int16_t s = (int16_t)rd_u16le(sp);
                        v = (float)s / 32768.0f;
                        break;
                    }
                    case 24: {
                        uint32_t u = (uint32_t)sp[0] | ((uint32_t)sp[1] << 8) | ((uint32_t)sp[2] << 16);
                        int32_t s = sign_extend_24(u);
                        v = (float)s / 8388608.0f;
                        break;
                    }
                    case 32: {
                        int32_t s = (int32_t)rd_u32le(sp);
                        v = (float)((double)s / 2147483648.0);
                        break;
                    }
                    default:
                        minirend_audio_buffer_destroy(out);
                        return NULL;
                }
            }
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            dst[i * (int)channels + c] = v;
        }
    }
    return out;
}


