#ifndef MINIREND_AUDIO_ENGINE_H
#define MINIREND_AUDIO_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MinirendAudioEngine MinirendAudioEngine;
typedef struct MinirendAudioNode   MinirendAudioNode;
typedef struct MinirendAudioParam  MinirendAudioParam;
typedef struct MinirendAudioBuffer MinirendAudioBuffer;

typedef enum {
    MINIREND_AUDIO_NODE_DESTINATION = 0,
    MINIREND_AUDIO_NODE_GAIN,
    MINIREND_AUDIO_NODE_OSCILLATOR,
    MINIREND_AUDIO_NODE_BUFFER_SOURCE,
    MINIREND_AUDIO_NODE_BIQUAD,
    MINIREND_AUDIO_NODE_ANALYSER,
    MINIREND_AUDIO_NODE_PANNER,
} MinirendAudioNodeType;

typedef enum {
    MINIREND_AUDIO_OSC_SINE = 0,
    MINIREND_AUDIO_OSC_SQUARE,
    MINIREND_AUDIO_OSC_SAWTOOTH,
    MINIREND_AUDIO_OSC_TRIANGLE,
} MinirendOscType;

typedef enum {
    MINIREND_BIQUAD_LOWPASS = 0,
    MINIREND_BIQUAD_HIGHPASS,
    MINIREND_BIQUAD_BANDPASS,
    MINIREND_BIQUAD_LOWSHELF,
    MINIREND_BIQUAD_HIGHSHELF,
    MINIREND_BIQUAD_PEAKING,
    MINIREND_BIQUAD_NOTCH,
    MINIREND_BIQUAD_ALLPASS,
} MinirendBiquadType;

typedef struct {
    float x, y, z;
} MinirendVec3;

/* Engine lifecycle */
MinirendAudioEngine* minirend_audio_engine_get(void);
void                 minirend_audio_engine_shutdown(void);

/* Advance audio and push samples to sokol_audio if running */
void minirend_audio_engine_tick(void);

/* Context / device control */
bool minirend_audio_engine_resume(MinirendAudioEngine* e);
bool minirend_audio_engine_suspend(MinirendAudioEngine* e);
bool minirend_audio_engine_close(MinirendAudioEngine* e);

int    minirend_audio_engine_sample_rate(MinirendAudioEngine* e);
double minirend_audio_engine_current_time(MinirendAudioEngine* e);

/* Nodes */
MinirendAudioNode* minirend_audio_node_create(MinirendAudioEngine* e, MinirendAudioNodeType type);
void              minirend_audio_node_retain(MinirendAudioNode* n);
void              minirend_audio_node_release(MinirendAudioEngine* e, MinirendAudioNode* n);

MinirendAudioNodeType minirend_audio_node_type(MinirendAudioNode* n);

bool minirend_audio_node_connect(MinirendAudioEngine* e, MinirendAudioNode* src, MinirendAudioNode* dst);
bool minirend_audio_node_disconnect(MinirendAudioEngine* e, MinirendAudioNode* src, MinirendAudioNode* dst);

/* Destination */
MinirendAudioNode* minirend_audio_engine_destination(MinirendAudioEngine* e);

/* GainNode */
MinirendAudioParam* minirend_audio_gain_param(MinirendAudioNode* gain);

/* OscillatorNode */
MinirendAudioParam* minirend_audio_osc_frequency(MinirendAudioNode* osc);
MinirendAudioParam* minirend_audio_osc_detune(MinirendAudioNode* osc);
void                minirend_audio_osc_set_type(MinirendAudioNode* osc, MinirendOscType t);
bool                minirend_audio_osc_start(MinirendAudioEngine* e, MinirendAudioNode* osc, double when);
bool                minirend_audio_osc_stop(MinirendAudioEngine* e, MinirendAudioNode* osc, double when);

/* AudioBuffer */
MinirendAudioBuffer* minirend_audio_buffer_create(int channels, int sample_rate, int frames);
void                 minirend_audio_buffer_destroy(MinirendAudioBuffer* b);
int                  minirend_audio_buffer_channels(MinirendAudioBuffer* b);
int                  minirend_audio_buffer_sample_rate(MinirendAudioBuffer* b);
int                  minirend_audio_buffer_length_frames(MinirendAudioBuffer* b);
float*               minirend_audio_buffer_data(MinirendAudioBuffer* b); /* interleaved */

/* BufferSourceNode */
void                minirend_audio_buffer_source_set_buffer(MinirendAudioNode* src, MinirendAudioBuffer* b);
MinirendAudioParam* minirend_audio_buffer_source_playback_rate(MinirendAudioNode* src);
bool                minirend_audio_buffer_source_start(MinirendAudioEngine* e, MinirendAudioNode* src, double when, double offset, double duration);
bool                minirend_audio_buffer_source_stop(MinirendAudioEngine* e, MinirendAudioNode* src, double when);
void                minirend_audio_buffer_source_set_loop(MinirendAudioNode* src, bool loop, double loop_start, double loop_end);

/* BiquadFilterNode */
void                minirend_audio_biquad_set_type(MinirendAudioNode* biquad, MinirendBiquadType t);
MinirendAudioParam* minirend_audio_biquad_frequency(MinirendAudioNode* biquad);
MinirendAudioParam* minirend_audio_biquad_q(MinirendAudioNode* biquad);
MinirendAudioParam* minirend_audio_biquad_gain(MinirendAudioNode* biquad);

/* AnalyserNode (lightweight) */
void     minirend_audio_analyser_set_fft_size(MinirendAudioNode* an, int fft_size);
int      minirend_audio_analyser_fft_size(MinirendAudioNode* an);
int      minirend_audio_analyser_frequency_bin_count(MinirendAudioNode* an);
uint8_t* minirend_audio_analyser_get_byte_time_domain(MinirendAudioNode* an, int* out_len);
uint8_t* minirend_audio_analyser_get_byte_frequency(MinirendAudioNode* an, int* out_len);

/* Spatial audio (Panner + Listener) */
MinirendAudioParam* minirend_audio_panner_position_x(MinirendAudioNode* pn);
MinirendAudioParam* minirend_audio_panner_position_y(MinirendAudioNode* pn);
MinirendAudioParam* minirend_audio_panner_position_z(MinirendAudioNode* pn);

MinirendAudioParam* minirend_audio_listener_position_x(MinirendAudioEngine* e);
MinirendAudioParam* minirend_audio_listener_position_y(MinirendAudioEngine* e);
MinirendAudioParam* minirend_audio_listener_position_z(MinirendAudioEngine* e);
MinirendAudioParam* minirend_audio_listener_forward_x(MinirendAudioEngine* e);
MinirendAudioParam* minirend_audio_listener_forward_y(MinirendAudioEngine* e);
MinirendAudioParam* minirend_audio_listener_forward_z(MinirendAudioEngine* e);
MinirendAudioParam* minirend_audio_listener_up_x(MinirendAudioEngine* e);
MinirendAudioParam* minirend_audio_listener_up_y(MinirendAudioEngine* e);
MinirendAudioParam* minirend_audio_listener_up_z(MinirendAudioEngine* e);

/* convenience setters */
void        minirend_audio_listener_set_position(MinirendAudioEngine* e, MinirendVec3 pos);
MinirendVec3 minirend_audio_listener_position(MinirendAudioEngine* e);

/* AudioParam automation */
double minirend_audio_param_value_at(MinirendAudioParam* p, double t);
double minirend_audio_param_default_value(MinirendAudioParam* p);
void   minirend_audio_param_set_value(MinirendAudioParam* p, double v);
void   minirend_audio_param_set_value_at_time(MinirendAudioParam* p, double v, double t);
void   minirend_audio_param_linear_ramp_to_value_at_time(MinirendAudioParam* p, double v, double t);
void   minirend_audio_param_exponential_ramp_to_value_at_time(MinirendAudioParam* p, double v, double t);
void   minirend_audio_param_set_target_at_time(MinirendAudioParam* p, double target, double start_time, double time_constant);

#endif /* MINIREND_AUDIO_ENGINE_H */


