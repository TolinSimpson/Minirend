#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_engine.h"
#include "sokol_audio.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef MINIREND_AUDIO_MAX_INPUTS
#define MINIREND_AUDIO_MAX_INPUTS 16
#endif

#ifndef MINIREND_AUDIO_MAX_EVENTS
#define MINIREND_AUDIO_MAX_EVENTS 64
#endif

typedef enum {
    PARAM_EVENT_SET_VALUE_AT_TIME = 0,
    PARAM_EVENT_LINEAR_RAMP,
    PARAM_EVENT_EXP_RAMP,
    PARAM_EVENT_SET_TARGET,
} ParamEventType;

typedef struct {
    ParamEventType type;
    double         time;
    double         v;
    double         time_constant; /* for setTargetAtTime */
} ParamEvent;

struct MinirendAudioParam {
    double     value;
    double     default_value;
    ParamEvent events[MINIREND_AUDIO_MAX_EVENTS];
    int        num_events;
};

typedef struct {
    int   channels;
    int   sample_rate;
    int   frames;
    float*data; /* interleaved */
} AudioBufferNative;

struct MinirendAudioBuffer {
    AudioBufferNative b;
};

typedef struct {
    /* per-channel state */
    float z1[2];
    float z2[2];
} BiquadState;

struct MinirendAudioNode {
    int                    refcount;
    MinirendAudioNodeType  type;

    struct MinirendAudioNode* inputs[MINIREND_AUDIO_MAX_INPUTS];
    int                     num_inputs;

    /* rendering cache */
    uint64_t  last_gen;
    float*    scratch;
    int       scratch_frames;

    union {
        struct { /* gain */
            struct MinirendAudioParam gain;
        } gain;
        struct { /* oscillator */
            MinirendOscType type;
            struct MinirendAudioParam frequency;
            struct MinirendAudioParam detune;
            double phase;
            bool   started;
            double start_time;
            double stop_time;
        } osc;
        struct { /* buffer source */
            struct MinirendAudioParam playback_rate;
            MinirendAudioBuffer* buffer;
            bool   loop;
            double loop_start;
            double loop_end;
            bool   started;
            double start_time;
            double stop_time;
            double offset_seconds;
            double duration_seconds;
        } buf;
        struct { /* biquad */
            MinirendBiquadType type;
            struct MinirendAudioParam frequency;
            struct MinirendAudioParam q;
            struct MinirendAudioParam gain;
            float a0, a1, a2, b1, b2;
            BiquadState st;
        } biquad;
        struct { /* analyser */
            int fft_size;
            uint8_t* td;
            int td_len;
            uint8_t* fd;
            int fd_len;
        } analyser;
        struct { /* panner */
            struct MinirendAudioParam pos_x;
            struct MinirendAudioParam pos_y;
            struct MinirendAudioParam pos_z;
        } panner;
    } u;
};

struct MinirendAudioEngine {
    bool   created;
    bool   running;
    bool   closed;
    int    sample_rate;
    int    channels;
    double current_time;

    /* Listener as AudioParams (WebAudio-style) */
    struct MinirendAudioParam listener_pos_x;
    struct MinirendAudioParam listener_pos_y;
    struct MinirendAudioParam listener_pos_z;
    struct MinirendAudioParam listener_fwd_x;
    struct MinirendAudioParam listener_fwd_y;
    struct MinirendAudioParam listener_fwd_z;
    struct MinirendAudioParam listener_up_x;
    struct MinirendAudioParam listener_up_y;
    struct MinirendAudioParam listener_up_z;

    MinirendAudioNode* destination;

    uint64_t gen;
};

static MinirendAudioEngine g_engine = {0};

static double clampd(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void param_init(MinirendAudioParam* p, double def) {
    memset(p, 0, sizeof(*p));
    p->value = def;
    p->default_value = def;
}

static int param_event_cmp(const void* a, const void* b) {
    const ParamEvent* ea = (const ParamEvent*)a;
    const ParamEvent* eb = (const ParamEvent*)b;
    if (ea->time < eb->time) return -1;
    if (ea->time > eb->time) return 1;
    return 0;
}

static void param_add_event(MinirendAudioParam* p, ParamEvent e) {
    if (!p) return;
    if (p->num_events >= MINIREND_AUDIO_MAX_EVENTS) {
        /* drop oldest */
        memmove(&p->events[0], &p->events[1], sizeof(p->events[0]) * (MINIREND_AUDIO_MAX_EVENTS - 1));
        p->num_events = MINIREND_AUDIO_MAX_EVENTS - 1;
    }
    p->events[p->num_events++] = e;
    qsort(p->events, (size_t)p->num_events, sizeof(p->events[0]), param_event_cmp);
}

static double param_eval(MinirendAudioParam* p, double t) {
    if (!p) return 0.0;
    if (p->num_events == 0) return p->value;

    /* Find last event at or before t and first event after t */
    int idx = -1;
    for (int i = 0; i < p->num_events; i++) {
        if (p->events[i].time <= t) idx = i;
        else break;
    }
    int next_idx = idx + 1;
    if (next_idx >= p->num_events) next_idx = -1;

    /* Resolve the 'previous' value */
    double prev_val = p->value;
    if (idx >= 0) {
        ParamEvent prev = p->events[idx];
        if (prev.type == PARAM_EVENT_SET_TARGET) {
            double dt = t - prev.time;
            if (dt <= 0) {
                prev_val = p->value;
            } else {
                double tau = (prev.time_constant <= 0.0) ? 0.001 : prev.time_constant;
                prev_val = prev.v + (p->value - prev.v) * exp(-dt / tau);
            }
        } else {
            prev_val = prev.v;
        }
    }

    /* If the next scheduled event is a ramp endpoint, interpolate towards it */
    if (next_idx >= 0) {
        ParamEvent next = p->events[next_idx];
        if (next.type == PARAM_EVENT_LINEAR_RAMP || next.type == PARAM_EVENT_EXP_RAMP) {
            double t0 = (idx >= 0) ? p->events[idx].time : 0.0;
            double t1 = next.time;
            if (t1 <= t0) return next.v;
            double u = clampd((t - t0) / (t1 - t0), 0.0, 1.0);
            if (next.type == PARAM_EVENT_LINEAR_RAMP) {
                return prev_val + (next.v - prev_val) * u;
            } else {
                double v0 = (prev_val <= 0.0) ? 0.000001 : prev_val;
                double v1 = (next.v <= 0.0) ? 0.000001 : next.v;
                return v0 * pow(v1 / v0, u);
            }
        }
    }

    return prev_val;
}

double minirend_audio_param_value_at(MinirendAudioParam* p, double t) { return param_eval(p, t); }
double minirend_audio_param_default_value(MinirendAudioParam* p) { return p ? p->default_value : 0.0; }
void minirend_audio_param_set_value(MinirendAudioParam* p, double v) {
    if (!p) return;
    p->value = v;
}
void minirend_audio_param_set_value_at_time(MinirendAudioParam* p, double v, double t) {
    param_add_event(p, (ParamEvent){ .type = PARAM_EVENT_SET_VALUE_AT_TIME, .time = t, .v = v });
}
void minirend_audio_param_linear_ramp_to_value_at_time(MinirendAudioParam* p, double v, double t) {
    param_add_event(p, (ParamEvent){ .type = PARAM_EVENT_LINEAR_RAMP, .time = t, .v = v });
}
void minirend_audio_param_exponential_ramp_to_value_at_time(MinirendAudioParam* p, double v, double t) {
    param_add_event(p, (ParamEvent){ .type = PARAM_EVENT_EXP_RAMP, .time = t, .v = v });
}
void minirend_audio_param_set_target_at_time(MinirendAudioParam* p, double target, double start_time, double time_constant) {
    param_add_event(p, (ParamEvent){ .type = PARAM_EVENT_SET_TARGET, .time = start_time, .v = target, .time_constant = time_constant });
}

static void node_ensure_scratch(MinirendAudioNode* n, int frames, int channels) {
    if (!n) return;
    int need = frames * channels;
    if (n->scratch && n->scratch_frames >= need) return;
    free(n->scratch);
    n->scratch = (float*)calloc((size_t)need, sizeof(float));
    n->scratch_frames = need;
}

static void node_remove_input(MinirendAudioNode* dst, MinirendAudioNode* src) {
    if (!dst || !src) return;
    for (int i = 0; i < dst->num_inputs; i++) {
        if (dst->inputs[i] == src) {
            memmove(&dst->inputs[i], &dst->inputs[i + 1], sizeof(dst->inputs[0]) * (size_t)(dst->num_inputs - i - 1));
            dst->num_inputs--;
            return;
        }
    }
}

static void node_add_input(MinirendAudioNode* dst, MinirendAudioNode* src) {
    if (!dst || !src) return;
    if (dst->num_inputs >= MINIREND_AUDIO_MAX_INPUTS) return;
    /* avoid duplicates */
    for (int i = 0; i < dst->num_inputs; i++) {
        if (dst->inputs[i] == src) return;
    }
    dst->inputs[dst->num_inputs++] = src;
}

static void node_render(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels);

static void render_destination(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels) {
    memset(out, 0, (size_t)(frames * channels) * sizeof(float));
    for (int i = 0; i < n->num_inputs; i++) {
        MinirendAudioNode* in = n->inputs[i];
        if (!in) continue;
        node_ensure_scratch(in, frames, channels);
        node_render(e, in, in->scratch, frames, channels);
        for (int s = 0; s < frames * channels; s++) {
            out[s] += in->scratch[s];
        }
    }
    for (int s = 0; s < frames * channels; s++) {
        out[s] = clampf(out[s], -1.0f, 1.0f);
    }
}

static void render_gain(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels) {
    if (n->num_inputs < 1 || !n->inputs[0]) {
        memset(out, 0, (size_t)(frames * channels) * sizeof(float));
        return;
    }
    MinirendAudioNode* in = n->inputs[0];
    node_ensure_scratch(in, frames, channels);
    node_render(e, in, in->scratch, frames, channels);
    double t0 = e->current_time;
    for (int f = 0; f < frames; f++) {
        double t = t0 + (double)f / (double)e->sample_rate;
        float g = (float)clampd(param_eval(&n->u.gain.gain, t), 0.0, 16.0);
        for (int c = 0; c < channels; c++) {
            out[f * channels + c] = in->scratch[f * channels + c] * g;
        }
    }
}

static float osc_wave(MinirendOscType t, double phase) {
    double x = phase - floor(phase);
    switch (t) {
        case MINIREND_AUDIO_OSC_SINE: return (float)sin(2.0 * M_PI * x);
        case MINIREND_AUDIO_OSC_SQUARE: return (x < 0.5) ? 1.0f : -1.0f;
        case MINIREND_AUDIO_OSC_SAWTOOTH: return (float)(2.0 * x - 1.0);
        case MINIREND_AUDIO_OSC_TRIANGLE: return (float)(4.0 * fabs(x - 0.5) - 1.0);
        default: return 0.0f;
    }
}

static void render_osc(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels) {
    memset(out, 0, (size_t)(frames * channels) * sizeof(float));
    if (!n->u.osc.started) return;
    double t0 = e->current_time;
    for (int f = 0; f < frames; f++) {
        double t = t0 + (double)f / (double)e->sample_rate;
        if (t < n->u.osc.start_time) continue;
        if (n->u.osc.stop_time > 0.0 && t >= n->u.osc.stop_time) continue;
        double freq = param_eval(&n->u.osc.frequency, t);
        double det  = param_eval(&n->u.osc.detune, t);
        double f_hz = freq * pow(2.0, det / 1200.0);
        if (f_hz < 0.0) f_hz = 0.0;
        n->u.osc.phase += f_hz / (double)e->sample_rate;
        float s = osc_wave(n->u.osc.type, n->u.osc.phase);
        for (int c = 0; c < channels; c++) {
            out[f * channels + c] = s;
        }
    }
}

static void render_buffer_source(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels) {
    memset(out, 0, (size_t)(frames * channels) * sizeof(float));
    if (!n->u.buf.started) return;
    if (!n->u.buf.buffer) return;

    MinirendAudioBuffer* buf = n->u.buf.buffer;
    int buf_ch = buf->b.channels;
    int buf_sr = buf->b.sample_rate;
    int buf_frames = buf->b.frames;
    float* data = buf->b.data;

    double t0 = e->current_time;
    double src_rate_ratio = (double)buf_sr / (double)e->sample_rate;

    for (int f = 0; f < frames; f++) {
        double t = t0 + (double)f / (double)e->sample_rate;
        if (t < n->u.buf.start_time) continue;
        if (n->u.buf.stop_time > 0.0 && t >= n->u.buf.stop_time) continue;
        double rel = t - n->u.buf.start_time;
        if (n->u.buf.duration_seconds > 0.0 && rel >= n->u.buf.duration_seconds) continue;
        double rate = param_eval(&n->u.buf.playback_rate, t);
        if (rate < 0.0) rate = 0.0;

        double src_pos = (n->u.buf.offset_seconds + rel * rate) * (double)buf_sr;
        if (n->u.buf.loop) {
            double ls = n->u.buf.loop_start * (double)buf_sr;
            double le = n->u.buf.loop_end > 0.0 ? n->u.buf.loop_end * (double)buf_sr : (double)buf_frames;
            if (le <= ls) le = (double)buf_frames;
            double span = le - ls;
            if (span > 1.0) {
                while (src_pos >= le) src_pos -= span;
                while (src_pos < ls) src_pos += span;
            }
        }

        int i0 = (int)floor(src_pos);
        double frac = src_pos - (double)i0;
        if (i0 < 0 || i0 >= buf_frames) continue;
        int i1 = i0 + 1;
        if (i1 >= buf_frames) i1 = buf_frames - 1;

        for (int c = 0; c < channels; c++) {
            int sc = (c < buf_ch) ? c : (buf_ch - 1);
            float s0 = data[i0 * buf_ch + sc];
            float s1 = data[i1 * buf_ch + sc];
            float s = (float)((1.0 - frac) * s0 + frac * s1);
            out[f * channels + c] = s;
        }

        (void)src_rate_ratio;
    }
}

static void biquad_recalc(MinirendAudioEngine* e, MinirendAudioNode* n) {
    double fs = (double)e->sample_rate;
    double f0 = clampd(param_eval(&n->u.biquad.frequency, e->current_time), 10.0, fs * 0.45);
    double q  = clampd(param_eval(&n->u.biquad.q, e->current_time), 0.0001, 1000.0);
    double gain_db = param_eval(&n->u.biquad.gain, e->current_time);
    double A = pow(10.0, gain_db / 40.0);

    double w0 = 2.0 * M_PI * f0 / fs;
    double cw = cos(w0);
    double sw = sin(w0);
    double alpha = sw / (2.0 * q);

    double b0, b1, b2, a0, a1, a2;
    switch (n->u.biquad.type) {
        case MINIREND_BIQUAD_HIGHPASS:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case MINIREND_BIQUAD_BANDPASS:
            b0 = sw / 2; b1 = 0; b2 = -sw / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case MINIREND_BIQUAD_NOTCH:
            b0 = 1; b1 = -2 * cw; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case MINIREND_BIQUAD_ALLPASS:
            b0 = 1 - alpha; b1 = -2 * cw; b2 = 1 + alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
        case MINIREND_BIQUAD_LOWSHELF: {
            double sqrtA = sqrt(A);
            double two_sqrtA_alpha = 2 * sqrtA * alpha;
            b0 =    A*((A+1) - (A-1)*cw + two_sqrtA_alpha);
            b1 =  2*A*((A-1) - (A+1)*cw);
            b2 =    A*((A+1) - (A-1)*cw - two_sqrtA_alpha);
            a0 =        (A+1) + (A-1)*cw + two_sqrtA_alpha;
            a1 =   -2*((A-1) + (A+1)*cw);
            a2 =        (A+1) + (A-1)*cw - two_sqrtA_alpha;
            break;
        }
        case MINIREND_BIQUAD_HIGHSHELF: {
            double sqrtA = sqrt(A);
            double two_sqrtA_alpha = 2 * sqrtA * alpha;
            b0 =    A*((A+1) + (A-1)*cw + two_sqrtA_alpha);
            b1 = -2*A*((A-1) + (A+1)*cw);
            b2 =    A*((A+1) + (A-1)*cw - two_sqrtA_alpha);
            a0 =        (A+1) - (A-1)*cw + two_sqrtA_alpha;
            a1 =    2*((A-1) - (A+1)*cw);
            a2 =        (A+1) - (A-1)*cw - two_sqrtA_alpha;
            break;
        }
        case MINIREND_BIQUAD_PEAKING: {
            b0 = 1 + alpha * A;
            b1 = -2 * cw;
            b2 = 1 - alpha * A;
            a0 = 1 + alpha / A;
            a1 = -2 * cw;
            a2 = 1 - alpha / A;
            break;
        }
        case MINIREND_BIQUAD_LOWPASS:
        default:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
            break;
    }

    n->u.biquad.a0 = (float)(b0 / a0);
    n->u.biquad.a1 = (float)(b1 / a0);
    n->u.biquad.a2 = (float)(b2 / a0);
    n->u.biquad.b1 = (float)(a1 / a0);
    n->u.biquad.b2 = (float)(a2 / a0);
}

static void render_biquad(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels) {
    if (n->num_inputs < 1 || !n->inputs[0]) {
        memset(out, 0, (size_t)(frames * channels) * sizeof(float));
        return;
    }
    MinirendAudioNode* in = n->inputs[0];
    node_ensure_scratch(in, frames, channels);
    node_render(e, in, in->scratch, frames, channels);

    biquad_recalc(e, n);
    float b0 = n->u.biquad.a0, b1 = n->u.biquad.a1, b2 = n->u.biquad.a2;
    float a1 = n->u.biquad.b1, a2 = n->u.biquad.b2;

    for (int f = 0; f < frames; f++) {
        for (int c = 0; c < channels; c++) {
            float x = in->scratch[f * channels + c];
            float y = b0 * x + n->u.biquad.st.z1[c];
            n->u.biquad.st.z1[c] = b1 * x - a1 * y + n->u.biquad.st.z2[c];
            n->u.biquad.st.z2[c] = b2 * x - a2 * y;
            out[f * channels + c] = y;
        }
    }
}

static void analyser_update(MinirendAudioNode* n, const float* in, int frames, int channels) {
    if (!n) return;
    int want = n->u.analyser.fft_size;
    if (want <= 0) want = 2048;
    if ((want & (want - 1)) != 0) want = 2048;
    n->u.analyser.fft_size = want;
    int td_len = want;
    int fd_len = want / 2;
    if (n->u.analyser.td_len != td_len) {
        free(n->u.analyser.td);
        n->u.analyser.td = (uint8_t*)malloc((size_t)td_len);
        n->u.analyser.td_len = td_len;
    }
    if (n->u.analyser.fd_len != fd_len) {
        free(n->u.analyser.fd);
        n->u.analyser.fd = (uint8_t*)malloc((size_t)fd_len);
        n->u.analyser.fd_len = fd_len;
    }
    if (!n->u.analyser.td || !n->u.analyser.fd) return;

    /* time-domain: map [-1,1] to [0,255], take channel 0 */
    for (int i = 0; i < td_len; i++) {
        int src = frames - td_len + i;
        float s = 0.0f;
        if (src >= 0 && src < frames) {
            s = in[src * channels + 0];
        }
        int v = (int)lrintf((s * 0.5f + 0.5f) * 255.0f);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        n->u.analyser.td[i] = (uint8_t)v;
    }
    /* frequency-domain: placeholder (silence) */
    memset(n->u.analyser.fd, 0, (size_t)fd_len);
}

static void render_analyser(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels) {
    if (n->num_inputs < 1 || !n->inputs[0]) {
        memset(out, 0, (size_t)(frames * channels) * sizeof(float));
        analyser_update(n, out, frames, channels);
        return;
    }
    MinirendAudioNode* in = n->inputs[0];
    node_ensure_scratch(in, frames, channels);
    node_render(e, in, in->scratch, frames, channels);
    memcpy(out, in->scratch, (size_t)(frames * channels) * sizeof(float));
    analyser_update(n, out, frames, channels);
}

static void render_panner(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels) {
    if (n->num_inputs < 1 || !n->inputs[0]) {
        memset(out, 0, (size_t)(frames * channels) * sizeof(float));
        return;
    }
    MinirendAudioNode* in = n->inputs[0];
    node_ensure_scratch(in, frames, channels);
    node_render(e, in, in->scratch, frames, channels);

    /* simple stereo pan based on x distance to listener */
    double t0 = e->current_time;
    for (int f = 0; f < frames; f++) {
        double t = t0 + (double)f / (double)e->sample_rate;
        double lx = param_eval(&e->listener_pos_x, t);
        double ly = param_eval(&e->listener_pos_y, t);
        double lz = param_eval(&e->listener_pos_z, t);
        double px = param_eval(&n->u.panner.pos_x, t);
        double py = param_eval(&n->u.panner.pos_y, t);
        double pz = param_eval(&n->u.panner.pos_z, t);
        double dx = px - lx;
        double dy = py - ly;
        double dz = pz - lz;
        double dist = sqrt(dx*dx + dy*dy + dz*dz);
        double att = 1.0 / (1.0 + dist); /* very simple attenuation */
        double pan = clampd(dx / 5.0, -1.0, 1.0);
        /* equal power */
        double angle = (pan + 1.0) * (M_PI / 4.0);
        float gl = (float)(cos(angle) * att);
        float gr = (float)(sin(angle) * att);

        float inL = in->scratch[f * channels + 0];
        float inR = (channels > 1) ? in->scratch[f * channels + 1] : inL;
        float mono = 0.5f * (inL + inR);
        out[f * channels + 0] = mono * gl;
        if (channels > 1) out[f * channels + 1] = mono * gr;
    }
}

static void node_render(MinirendAudioEngine* e, MinirendAudioNode* n, float* out, int frames, int channels) {
    if (!e || !n || !out) return;
    if (n->last_gen == e->gen) {
        memcpy(out, n->scratch, (size_t)(frames * channels) * sizeof(float));
        return;
    }
    node_ensure_scratch(n, frames, channels);
    switch (n->type) {
        case MINIREND_AUDIO_NODE_DESTINATION: render_destination(e, n, n->scratch, frames, channels); break;
        case MINIREND_AUDIO_NODE_GAIN:        render_gain(e, n, n->scratch, frames, channels); break;
        case MINIREND_AUDIO_NODE_OSCILLATOR:  render_osc(e, n, n->scratch, frames, channels); break;
        case MINIREND_AUDIO_NODE_BUFFER_SOURCE: render_buffer_source(e, n, n->scratch, frames, channels); break;
        case MINIREND_AUDIO_NODE_BIQUAD:      render_biquad(e, n, n->scratch, frames, channels); break;
        case MINIREND_AUDIO_NODE_ANALYSER:    render_analyser(e, n, n->scratch, frames, channels); break;
        case MINIREND_AUDIO_NODE_PANNER:      render_panner(e, n, n->scratch, frames, channels); break;
        default:
            memset(n->scratch, 0, (size_t)(frames * channels) * sizeof(float));
            break;
    }
    n->last_gen = e->gen;
    memcpy(out, n->scratch, (size_t)(frames * channels) * sizeof(float));
}

static MinirendAudioNode* node_alloc(MinirendAudioNodeType type) {
    MinirendAudioNode* n = (MinirendAudioNode*)calloc(1, sizeof(MinirendAudioNode));
    if (!n) return NULL;
    n->refcount = 1;
    n->type = type;
    n->last_gen = 0;
    n->scratch = NULL;
    n->scratch_frames = 0;
    switch (type) {
        case MINIREND_AUDIO_NODE_GAIN:
            param_init(&n->u.gain.gain, 1.0);
            break;
        case MINIREND_AUDIO_NODE_OSCILLATOR:
            n->u.osc.type = MINIREND_AUDIO_OSC_SINE;
            param_init(&n->u.osc.frequency, 440.0);
            param_init(&n->u.osc.detune, 0.0);
            n->u.osc.phase = 0.0;
            n->u.osc.started = false;
            n->u.osc.start_time = 0.0;
            n->u.osc.stop_time = 0.0;
            break;
        case MINIREND_AUDIO_NODE_BUFFER_SOURCE:
            param_init(&n->u.buf.playback_rate, 1.0);
            n->u.buf.buffer = NULL;
            n->u.buf.loop = false;
            n->u.buf.loop_start = 0.0;
            n->u.buf.loop_end = 0.0;
            n->u.buf.started = false;
            n->u.buf.start_time = 0.0;
            n->u.buf.stop_time = 0.0;
            n->u.buf.offset_seconds = 0.0;
            n->u.buf.duration_seconds = 0.0;
            break;
        case MINIREND_AUDIO_NODE_BIQUAD:
            n->u.biquad.type = MINIREND_BIQUAD_LOWPASS;
            param_init(&n->u.biquad.frequency, 350.0);
            param_init(&n->u.biquad.q, 1.0);
            param_init(&n->u.biquad.gain, 0.0);
            memset(&n->u.biquad.st, 0, sizeof(n->u.biquad.st));
            break;
        case MINIREND_AUDIO_NODE_ANALYSER:
            n->u.analyser.fft_size = 2048;
            n->u.analyser.td = NULL; n->u.analyser.td_len = 0;
            n->u.analyser.fd = NULL; n->u.analyser.fd_len = 0;
            break;
        case MINIREND_AUDIO_NODE_PANNER:
            param_init(&n->u.panner.pos_x, 0.0);
            param_init(&n->u.panner.pos_y, 0.0);
            param_init(&n->u.panner.pos_z, 0.0);
            break;
        default:
            break;
    }
    return n;
}

MinirendAudioEngine* minirend_audio_engine_get(void) {
    if (!g_engine.created) {
        g_engine.created = true;
        g_engine.running = false;
        g_engine.closed  = false;
        g_engine.sample_rate = 44100;
        g_engine.channels = 2;
        g_engine.current_time = 0.0;
        param_init(&g_engine.listener_pos_x, 0.0);
        param_init(&g_engine.listener_pos_y, 0.0);
        param_init(&g_engine.listener_pos_z, 0.0);
        param_init(&g_engine.listener_fwd_x, 0.0);
        param_init(&g_engine.listener_fwd_y, 0.0);
        param_init(&g_engine.listener_fwd_z, -1.0);
        param_init(&g_engine.listener_up_x, 0.0);
        param_init(&g_engine.listener_up_y, 1.0);
        param_init(&g_engine.listener_up_z, 0.0);
        g_engine.destination = node_alloc(MINIREND_AUDIO_NODE_DESTINATION);
        g_engine.gen = 1;
    }
    return &g_engine;
}

void minirend_audio_engine_shutdown(void) {
    if (!g_engine.created) return;
    if (saudio_isvalid()) {
        saudio_shutdown();
    }
    if (g_engine.destination) {
        free(g_engine.destination->scratch);
        free(g_engine.destination);
        g_engine.destination = NULL;
    }
    g_engine.created = false;
}

bool minirend_audio_engine_resume(MinirendAudioEngine* e) {
    if (!e || e->closed) return false;
    if (!saudio_isvalid()) {
        saudio_setup(&(saudio_desc){
            .sample_rate = e->sample_rate,
            .num_channels = e->channels,
        });
        if (!saudio_isvalid()) return false;
        /* Query actual parameters */
        e->sample_rate = saudio_sample_rate();
        e->channels = saudio_channels();
    }
    e->running = true;
    return true;
}

bool minirend_audio_engine_suspend(MinirendAudioEngine* e) {
    if (!e || e->closed) return false;
    e->running = false;
    return true;
}

bool minirend_audio_engine_close(MinirendAudioEngine* e) {
    if (!e) return false;
    e->running = false;
    e->closed = true;
    if (saudio_isvalid()) saudio_shutdown();
    return true;
}

int minirend_audio_engine_sample_rate(MinirendAudioEngine* e) {
    if (!e) return 0;
    return e->sample_rate;
}

double minirend_audio_engine_current_time(MinirendAudioEngine* e) {
    if (!e) return 0.0;
    return e->current_time;
}

void minirend_audio_engine_tick(void) {
    MinirendAudioEngine* e = minirend_audio_engine_get();
    if (!e || !e->running || e->closed) return;
    if (!saudio_isvalid()) return;

    int frames = saudio_expect();
    if (frames <= 0) return;

    int channels = e->channels;
    float* mix = (float*)calloc((size_t)(frames * channels), sizeof(float));
    if (!mix) return;

    e->gen++;
    if (e->destination) {
        node_render(e, e->destination, mix, frames, channels);
    }
    saudio_push(mix, frames);
    e->current_time += (double)frames / (double)e->sample_rate;

    free(mix);
}

MinirendAudioNode* minirend_audio_engine_destination(MinirendAudioEngine* e) {
    if (!e) return NULL;
    return e->destination;
}

MinirendAudioNode* minirend_audio_node_create(MinirendAudioEngine* e, MinirendAudioNodeType type) {
    (void)e;
    return node_alloc(type);
}

void minirend_audio_node_retain(MinirendAudioNode* n) {
    if (!n) return;
    n->refcount++;
}

void minirend_audio_node_release(MinirendAudioEngine* e, MinirendAudioNode* n) {
    (void)e;
    if (!n) return;
    n->refcount--;
    if (n->refcount > 0) return;
    /* disconnect inputs (and release their refs) */
    for (int i = 0; i < n->num_inputs; i++) {
        if (n->inputs[i]) minirend_audio_node_release(e, n->inputs[i]);
        n->inputs[i] = NULL;
    }
    n->num_inputs = 0;
    free(n->scratch);
    if (n->type == MINIREND_AUDIO_NODE_ANALYSER) {
        free(n->u.analyser.td);
        free(n->u.analyser.fd);
    }
    free(n);
}

MinirendAudioNodeType minirend_audio_node_type(MinirendAudioNode* n) {
    if (!n) return MINIREND_AUDIO_NODE_DESTINATION;
    return n->type;
}

bool minirend_audio_node_connect(MinirendAudioEngine* e, MinirendAudioNode* src, MinirendAudioNode* dst) {
    (void)e;
    if (!src || !dst) return false;
    node_add_input(dst, src);
    minirend_audio_node_retain(src);
    return true;
}

bool minirend_audio_node_disconnect(MinirendAudioEngine* e, MinirendAudioNode* src, MinirendAudioNode* dst) {
    (void)e;
    if (!src || !dst) return false;
    node_remove_input(dst, src);
    minirend_audio_node_release(e, src);
    return true;
}

MinirendAudioParam* minirend_audio_gain_param(MinirendAudioNode* gain) {
    if (!gain || gain->type != MINIREND_AUDIO_NODE_GAIN) return NULL;
    return &gain->u.gain.gain;
}

MinirendAudioParam* minirend_audio_osc_frequency(MinirendAudioNode* osc) {
    if (!osc || osc->type != MINIREND_AUDIO_NODE_OSCILLATOR) return NULL;
    return &osc->u.osc.frequency;
}
MinirendAudioParam* minirend_audio_osc_detune(MinirendAudioNode* osc) {
    if (!osc || osc->type != MINIREND_AUDIO_NODE_OSCILLATOR) return NULL;
    return &osc->u.osc.detune;
}
void minirend_audio_osc_set_type(MinirendAudioNode* osc, MinirendOscType t) {
    if (!osc || osc->type != MINIREND_AUDIO_NODE_OSCILLATOR) return;
    osc->u.osc.type = t;
}
bool minirend_audio_osc_start(MinirendAudioEngine* e, MinirendAudioNode* osc, double when) {
    (void)e;
    if (!osc || osc->type != MINIREND_AUDIO_NODE_OSCILLATOR) return false;
    osc->u.osc.started = true;
    osc->u.osc.start_time = when;
    osc->u.osc.stop_time = 0.0;
    return true;
}
bool minirend_audio_osc_stop(MinirendAudioEngine* e, MinirendAudioNode* osc, double when) {
    (void)e;
    if (!osc || osc->type != MINIREND_AUDIO_NODE_OSCILLATOR) return false;
    osc->u.osc.stop_time = when;
    return true;
}

MinirendAudioBuffer* minirend_audio_buffer_create(int channels, int sample_rate, int frames) {
    if (channels <= 0 || channels > 8 || sample_rate <= 0 || frames < 0) return NULL;
    MinirendAudioBuffer* b = (MinirendAudioBuffer*)calloc(1, sizeof(MinirendAudioBuffer));
    if (!b) return NULL;
    b->b.channels = channels;
    b->b.sample_rate = sample_rate;
    b->b.frames = frames;
    b->b.data = (float*)calloc((size_t)channels * (size_t)frames, sizeof(float));
    if (!b->b.data) {
        free(b);
        return NULL;
    }
    return b;
}
void minirend_audio_buffer_destroy(MinirendAudioBuffer* b) {
    if (!b) return;
    free(b->b.data);
    free(b);
}
int minirend_audio_buffer_channels(MinirendAudioBuffer* b) { return b ? b->b.channels : 0; }
int minirend_audio_buffer_sample_rate(MinirendAudioBuffer* b) { return b ? b->b.sample_rate : 0; }
int minirend_audio_buffer_length_frames(MinirendAudioBuffer* b) { return b ? b->b.frames : 0; }
float* minirend_audio_buffer_data(MinirendAudioBuffer* b) { return b ? b->b.data : NULL; }

void minirend_audio_buffer_source_set_buffer(MinirendAudioNode* src, MinirendAudioBuffer* b) {
    if (!src || src->type != MINIREND_AUDIO_NODE_BUFFER_SOURCE) return;
    src->u.buf.buffer = b;
}
MinirendAudioParam* minirend_audio_buffer_source_playback_rate(MinirendAudioNode* src) {
    if (!src || src->type != MINIREND_AUDIO_NODE_BUFFER_SOURCE) return NULL;
    return &src->u.buf.playback_rate;
}
bool minirend_audio_buffer_source_start(MinirendAudioEngine* e, MinirendAudioNode* src, double when, double offset, double duration) {
    (void)e;
    if (!src || src->type != MINIREND_AUDIO_NODE_BUFFER_SOURCE) return false;
    src->u.buf.started = true;
    src->u.buf.start_time = when;
    src->u.buf.stop_time = 0.0;
    src->u.buf.offset_seconds = (offset < 0.0) ? 0.0 : offset;
    src->u.buf.duration_seconds = (duration <= 0.0) ? 0.0 : duration;
    return true;
}
bool minirend_audio_buffer_source_stop(MinirendAudioEngine* e, MinirendAudioNode* src, double when) {
    (void)e;
    if (!src || src->type != MINIREND_AUDIO_NODE_BUFFER_SOURCE) return false;
    src->u.buf.stop_time = when;
    return true;
}
void minirend_audio_buffer_source_set_loop(MinirendAudioNode* src, bool loop, double loop_start, double loop_end) {
    if (!src || src->type != MINIREND_AUDIO_NODE_BUFFER_SOURCE) return;
    src->u.buf.loop = loop;
    src->u.buf.loop_start = loop_start;
    src->u.buf.loop_end = loop_end;
}

void minirend_audio_biquad_set_type(MinirendAudioNode* biquad, MinirendBiquadType t) {
    if (!biquad || biquad->type != MINIREND_AUDIO_NODE_BIQUAD) return;
    biquad->u.biquad.type = t;
}
MinirendAudioParam* minirend_audio_biquad_frequency(MinirendAudioNode* biquad) {
    if (!biquad || biquad->type != MINIREND_AUDIO_NODE_BIQUAD) return NULL;
    return &biquad->u.biquad.frequency;
}
MinirendAudioParam* minirend_audio_biquad_q(MinirendAudioNode* biquad) {
    if (!biquad || biquad->type != MINIREND_AUDIO_NODE_BIQUAD) return NULL;
    return &biquad->u.biquad.q;
}
MinirendAudioParam* minirend_audio_biquad_gain(MinirendAudioNode* biquad) {
    if (!biquad || biquad->type != MINIREND_AUDIO_NODE_BIQUAD) return NULL;
    return &biquad->u.biquad.gain;
}

void minirend_audio_analyser_set_fft_size(MinirendAudioNode* an, int fft_size) {
    if (!an || an->type != MINIREND_AUDIO_NODE_ANALYSER) return;
    an->u.analyser.fft_size = fft_size;
}
int minirend_audio_analyser_fft_size(MinirendAudioNode* an) {
    if (!an || an->type != MINIREND_AUDIO_NODE_ANALYSER) return 0;
    return an->u.analyser.fft_size;
}
int minirend_audio_analyser_frequency_bin_count(MinirendAudioNode* an) {
    if (!an || an->type != MINIREND_AUDIO_NODE_ANALYSER) return 0;
    int fft = an->u.analyser.fft_size;
    return (fft > 0) ? (fft / 2) : 0;
}
uint8_t* minirend_audio_analyser_get_byte_time_domain(MinirendAudioNode* an, int* out_len) {
    if (!an || an->type != MINIREND_AUDIO_NODE_ANALYSER) return NULL;
    if (out_len) *out_len = an->u.analyser.td_len;
    return an->u.analyser.td;
}
uint8_t* minirend_audio_analyser_get_byte_frequency(MinirendAudioNode* an, int* out_len) {
    if (!an || an->type != MINIREND_AUDIO_NODE_ANALYSER) return NULL;
    if (out_len) *out_len = an->u.analyser.fd_len;
    return an->u.analyser.fd;
}

MinirendAudioParam* minirend_audio_panner_position_x(MinirendAudioNode* pn) {
    if (!pn || pn->type != MINIREND_AUDIO_NODE_PANNER) return NULL;
    return &pn->u.panner.pos_x;
}
MinirendAudioParam* minirend_audio_panner_position_y(MinirendAudioNode* pn) {
    if (!pn || pn->type != MINIREND_AUDIO_NODE_PANNER) return NULL;
    return &pn->u.panner.pos_y;
}
MinirendAudioParam* minirend_audio_panner_position_z(MinirendAudioNode* pn) {
    if (!pn || pn->type != MINIREND_AUDIO_NODE_PANNER) return NULL;
    return &pn->u.panner.pos_z;
}

MinirendAudioParam* minirend_audio_listener_position_x(MinirendAudioEngine* e) { return e ? &e->listener_pos_x : NULL; }
MinirendAudioParam* minirend_audio_listener_position_y(MinirendAudioEngine* e) { return e ? &e->listener_pos_y : NULL; }
MinirendAudioParam* minirend_audio_listener_position_z(MinirendAudioEngine* e) { return e ? &e->listener_pos_z : NULL; }
MinirendAudioParam* minirend_audio_listener_forward_x(MinirendAudioEngine* e) { return e ? &e->listener_fwd_x : NULL; }
MinirendAudioParam* minirend_audio_listener_forward_y(MinirendAudioEngine* e) { return e ? &e->listener_fwd_y : NULL; }
MinirendAudioParam* minirend_audio_listener_forward_z(MinirendAudioEngine* e) { return e ? &e->listener_fwd_z : NULL; }
MinirendAudioParam* minirend_audio_listener_up_x(MinirendAudioEngine* e) { return e ? &e->listener_up_x : NULL; }
MinirendAudioParam* minirend_audio_listener_up_y(MinirendAudioEngine* e) { return e ? &e->listener_up_y : NULL; }
MinirendAudioParam* minirend_audio_listener_up_z(MinirendAudioEngine* e) { return e ? &e->listener_up_z : NULL; }

void minirend_audio_listener_set_position(MinirendAudioEngine* e, MinirendVec3 pos) {
    if (!e) return;
    minirend_audio_param_set_value(&e->listener_pos_x, pos.x);
    minirend_audio_param_set_value(&e->listener_pos_y, pos.y);
    minirend_audio_param_set_value(&e->listener_pos_z, pos.z);
}
MinirendVec3 minirend_audio_listener_position(MinirendAudioEngine* e) {
    if (!e) return (MinirendVec3){0,0,0};
    return (MinirendVec3){
        (float)e->listener_pos_x.value,
        (float)e->listener_pos_y.value,
        (float)e->listener_pos_z.value,
    };
}


