#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minirend.h"
#include "quickjs.h"

#include "audio_engine.h"
#include "audio_buffer.h"

typedef struct {
    MinirendAudioEngine* e;
} AudioContextWrap;

typedef struct {
    MinirendAudioEngine* e;
    MinirendAudioNode*   n;
} AudioNodeWrap;

typedef struct {
    MinirendAudioEngine* e;
    MinirendAudioNode*   owner;
    MinirendAudioParam*  p;
} AudioParamWrap;

typedef struct {
    MinirendAudioBuffer* b;
} AudioBufferWrap;

static JSClassID js_audio_ctx_class_id;
static JSClassID js_audio_node_class_id;
static JSClassID js_audio_param_class_id;
static JSClassID js_audio_buffer_class_id;

static JSValue audio_ctx_proto;
static JSValue audio_node_proto;
static JSValue audio_param_proto;
static JSValue audio_buffer_proto;

static void js_audio_ctx_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    AudioContextWrap* w = (AudioContextWrap*)JS_GetOpaque(val, js_audio_ctx_class_id);
    if (w) {
        free(w);
    }
}

static void js_audio_node_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    AudioNodeWrap* w = (AudioNodeWrap*)JS_GetOpaque(val, js_audio_node_class_id);
    if (w) {
        if (w->e && w->n) {
            minirend_audio_node_release(w->e, w->n);
        }
        free(w);
    }
}

static void js_audio_param_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    AudioParamWrap* w = (AudioParamWrap*)JS_GetOpaque(val, js_audio_param_class_id);
    if (w) {
        if (w->e && w->owner) {
            minirend_audio_node_release(w->e, w->owner);
        }
        free(w);
    }
}

static void js_audio_buffer_finalizer(JSRuntime* rt, JSValue val) {
    (void)rt;
    AudioBufferWrap* w = (AudioBufferWrap*)JS_GetOpaque(val, js_audio_buffer_class_id);
    if (w) {
        if (w->b) minirend_audio_buffer_destroy(w->b);
        free(w);
    }
}

static JSClassDef js_audio_ctx_class = { "AudioContext", .finalizer = js_audio_ctx_finalizer };
static JSClassDef js_audio_node_class = { "AudioNode", .finalizer = js_audio_node_finalizer };
static JSClassDef js_audio_param_class = { "AudioParam", .finalizer = js_audio_param_finalizer };
static JSClassDef js_audio_buffer_class = { "AudioBuffer", .finalizer = js_audio_buffer_finalizer };

static AudioContextWrap* get_audio_ctx(JSContext* ctx, JSValueConst v) {
    return (AudioContextWrap*)JS_GetOpaque(v, js_audio_ctx_class_id);
}

static AudioNodeWrap* get_audio_node(JSContext* ctx, JSValueConst v) {
    return (AudioNodeWrap*)JS_GetOpaque(v, js_audio_node_class_id);
}

static AudioParamWrap* get_audio_param(JSContext* ctx, JSValueConst v) {
    return (AudioParamWrap*)JS_GetOpaque(v, js_audio_param_class_id);
}

static JSValue make_audio_param(JSContext* ctx, MinirendAudioEngine* e, MinirendAudioNode* owner, MinirendAudioParam* p) {
    AudioParamWrap* w = (AudioParamWrap*)calloc(1, sizeof(*w));
    if (!w) return JS_EXCEPTION;
    w->e = e;
    w->owner = owner;
    w->p = p;
    minirend_audio_node_retain(owner);

    JSValue obj = JS_NewObjectClass(ctx, js_audio_param_class_id);
    if (JS_IsException(obj)) {
        minirend_audio_node_release(e, owner);
        free(w);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, w);
    JS_SetPrototype(ctx, obj, audio_param_proto);
    return obj;
}

static JSValue make_audio_node(JSContext* ctx, MinirendAudioEngine* e, MinirendAudioNode* n) {
    AudioNodeWrap* w = (AudioNodeWrap*)calloc(1, sizeof(*w));
    if (!w) return JS_EXCEPTION;
    w->e = e;
    w->n = n;

    JSValue obj = JS_NewObjectClass(ctx, js_audio_node_class_id);
    if (JS_IsException(obj)) {
        minirend_audio_node_release(e, n);
        free(w);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, w);
    JS_SetPrototype(ctx, obj, audio_node_proto);
    return obj;
}

static JSValue make_audio_buffer(JSContext* ctx, MinirendAudioBuffer* b) {
    AudioBufferWrap* w = (AudioBufferWrap*)calloc(1, sizeof(*w));
    if (!w) return JS_EXCEPTION;
    w->b = b;
    JSValue obj = JS_NewObjectClass(ctx, js_audio_buffer_class_id);
    if (JS_IsException(obj)) {
        minirend_audio_buffer_destroy(b);
        free(w);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, w);
    JS_SetPrototype(ctx, obj, audio_buffer_proto);
    return obj;
}

static AudioBufferWrap* get_audio_buffer(JSContext* ctx, JSValueConst v) {
    return (AudioBufferWrap*)JS_GetOpaque(v, js_audio_buffer_class_id);
}

/* =========================
 * AudioParam methods
 * ========================= */

static JSValue js_audio_param_get_value(JSContext* ctx, JSValueConst this_val) {
    AudioParamWrap* w = get_audio_param(ctx, this_val);
    if (!w || !w->p) return JS_UNDEFINED;
    double v = minirend_audio_param_value_at(w->p, minirend_audio_engine_current_time(w->e));
    return JS_NewFloat64(ctx, v);
}

static JSValue js_audio_param_get_defaultValue(JSContext* ctx, JSValueConst this_val) {
    AudioParamWrap* w = get_audio_param(ctx, this_val);
    if (!w || !w->p) return JS_UNDEFINED;
    return JS_NewFloat64(ctx, minirend_audio_param_default_value(w->p));
}

static JSValue js_audio_param_get_minValue(JSContext* ctx, JSValueConst this_val) {
    (void)this_val;
    return JS_NewFloat64(ctx, -1e9);
}

static JSValue js_audio_param_get_maxValue(JSContext* ctx, JSValueConst this_val) {
    (void)this_val;
    return JS_NewFloat64(ctx, 1e9);
}

static JSValue js_audio_param_set_value(JSContext* ctx, JSValueConst this_val, JSValueConst v) {
    AudioParamWrap* w = get_audio_param(ctx, this_val);
    if (!w || !w->p) return JS_UNDEFINED;
    double dv = 0;
    if (JS_ToFloat64(ctx, &dv, v) != 0) return JS_ThrowTypeError(ctx, "invalid value");
    minirend_audio_param_set_value(w->p, dv);
    return JS_UNDEFINED;
}

static JSValue js_audio_param_setValueAtTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioParamWrap* w = get_audio_param(ctx, this_val);
    if (!w || !w->p) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "value,time required");
    double v = 0, t = 0;
    if (JS_ToFloat64(ctx, &v, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid value");
    if (JS_ToFloat64(ctx, &t, argv[1]) != 0) return JS_ThrowTypeError(ctx, "invalid time");
    minirend_audio_param_set_value_at_time(w->p, v, t);
    return JS_DupValue(ctx, this_val);
}

static JSValue js_audio_param_linearRampToValueAtTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioParamWrap* w = get_audio_param(ctx, this_val);
    if (!w || !w->p) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "value,time required");
    double v = 0, t = 0;
    if (JS_ToFloat64(ctx, &v, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid value");
    if (JS_ToFloat64(ctx, &t, argv[1]) != 0) return JS_ThrowTypeError(ctx, "invalid time");
    minirend_audio_param_linear_ramp_to_value_at_time(w->p, v, t);
    return JS_DupValue(ctx, this_val);
}

static JSValue js_audio_param_exponentialRampToValueAtTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioParamWrap* w = get_audio_param(ctx, this_val);
    if (!w || !w->p) return JS_EXCEPTION;
    if (argc < 2) return JS_ThrowTypeError(ctx, "value,time required");
    double v = 0, t = 0;
    if (JS_ToFloat64(ctx, &v, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid value");
    if (JS_ToFloat64(ctx, &t, argv[1]) != 0) return JS_ThrowTypeError(ctx, "invalid time");
    minirend_audio_param_exponential_ramp_to_value_at_time(w->p, v, t);
    return JS_DupValue(ctx, this_val);
}

static JSValue js_audio_param_setTargetAtTime(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioParamWrap* w = get_audio_param(ctx, this_val);
    if (!w || !w->p) return JS_EXCEPTION;
    if (argc < 3) return JS_ThrowTypeError(ctx, "target,startTime,timeConstant required");
    double target = 0, start_time = 0, tc = 0;
    if (JS_ToFloat64(ctx, &target, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid target");
    if (JS_ToFloat64(ctx, &start_time, argv[1]) != 0) return JS_ThrowTypeError(ctx, "invalid startTime");
    if (JS_ToFloat64(ctx, &tc, argv[2]) != 0) return JS_ThrowTypeError(ctx, "invalid timeConstant");
    minirend_audio_param_set_target_at_time(w->p, target, start_time, tc);
    return JS_DupValue(ctx, this_val);
}

/* =========================
 * AudioNode methods
 * ========================= */

static JSValue js_audio_node_connect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioNodeWrap* src = get_audio_node(ctx, this_val);
    if (!src || !src->e || !src->n) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "destination required");
    AudioNodeWrap* dst = get_audio_node(ctx, argv[0]);
    if (!dst || !dst->n) return JS_ThrowTypeError(ctx, "invalid destination");
    if (!minirend_audio_node_connect(src->e, src->n, dst->n)) {
        return JS_ThrowInternalError(ctx, "connect failed");
    }
    return JS_DupValue(ctx, argv[0]);
}

static JSValue js_audio_node_disconnect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioNodeWrap* src = get_audio_node(ctx, this_val);
    if (!src || !src->e || !src->n) return JS_EXCEPTION;
    if (argc < 1) return JS_UNDEFINED;
    AudioNodeWrap* dst = get_audio_node(ctx, argv[0]);
    if (!dst || !dst->n) return JS_UNDEFINED;
    minirend_audio_node_disconnect(src->e, src->n, dst->n);
    return JS_UNDEFINED;
}

/* =========================
 * Node specific accessors
 * ========================= */

static JSValue js_gain_get_gain(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_gain_param(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}

static JSValue js_osc_get_frequency(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_osc_frequency(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}

static JSValue js_osc_get_detune(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_osc_detune(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}

static JSValue js_osc_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_EXCEPTION;
    double when = minirend_audio_engine_current_time(w->e);
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToFloat64(ctx, &when, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid when");
    }
    minirend_audio_osc_start(w->e, w->n, when);
    return JS_UNDEFINED;
}

static JSValue js_osc_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_EXCEPTION;
    double when = minirend_audio_engine_current_time(w->e);
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToFloat64(ctx, &when, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid when");
    }
    minirend_audio_osc_stop(w->e, w->n, when);
    return JS_UNDEFINED;
}

static JSValue js_osc_get_type(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    /* Expose as string per WebAudio */
    /* We don't have public getter in engine; infer by param pointer equality not possible, so store as hidden prop. */
    return JS_GetPropertyStr(ctx, this_val, "__oscType");
}

static JSValue js_osc_set_type(JSContext* ctx, JSValueConst this_val, JSValueConst v) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    const char* s = JS_ToCString(ctx, v);
    if (!s) return JS_EXCEPTION;
    MinirendOscType t = MINIREND_AUDIO_OSC_SINE;
    if (strcmp(s, "square") == 0) t = MINIREND_AUDIO_OSC_SQUARE;
    else if (strcmp(s, "sawtooth") == 0) t = MINIREND_AUDIO_OSC_SAWTOOTH;
    else if (strcmp(s, "triangle") == 0) t = MINIREND_AUDIO_OSC_TRIANGLE;
    minirend_audio_osc_set_type(w->n, t);
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__oscType", JS_NewString(ctx, s));
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

static JSValue js_biquad_get_frequency(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_biquad_frequency(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}

static JSValue js_biquad_get_q(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_biquad_q(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}

static JSValue js_biquad_get_gain(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_biquad_gain(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}

static JSValue js_biquad_get_type(JSContext* ctx, JSValueConst this_val) {
    return JS_GetPropertyStr(ctx, this_val, "__biquadType");
}

static JSValue js_biquad_set_type(JSContext* ctx, JSValueConst this_val, JSValueConst v) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    const char* s = JS_ToCString(ctx, v);
    if (!s) return JS_EXCEPTION;
    MinirendBiquadType t = MINIREND_BIQUAD_LOWPASS;
    if (strcmp(s, "highpass") == 0) t = MINIREND_BIQUAD_HIGHPASS;
    else if (strcmp(s, "bandpass") == 0) t = MINIREND_BIQUAD_BANDPASS;
    else if (strcmp(s, "lowshelf") == 0) t = MINIREND_BIQUAD_LOWSHELF;
    else if (strcmp(s, "highshelf") == 0) t = MINIREND_BIQUAD_HIGHSHELF;
    else if (strcmp(s, "peaking") == 0) t = MINIREND_BIQUAD_PEAKING;
    else if (strcmp(s, "notch") == 0) t = MINIREND_BIQUAD_NOTCH;
    else if (strcmp(s, "allpass") == 0) t = MINIREND_BIQUAD_ALLPASS;
    minirend_audio_biquad_set_type(w->n, t);
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__biquadType", JS_NewString(ctx, s));
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

static JSValue js_panner_get_positionX(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_panner_position_x(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}
static JSValue js_panner_get_positionY(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_panner_position_y(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}
static JSValue js_panner_get_positionZ(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_panner_position_z(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}

static int get_uint8array_bytes(JSContext* ctx, JSValueConst v, uint8_t** out, size_t* out_size) {
    uint8_t* data = NULL;
    size_t size = 0;
    data = JS_GetArrayBuffer(ctx, &size, v);
    if (data) {
        *out = data;
        *out_size = size;
        return 1;
    }
    size_t byte_off = 0, byte_len = 0, bpe = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, v, &byte_off, &byte_len, &bpe);
    if (JS_IsException(ab) || JS_IsNull(ab) || JS_IsUndefined(ab)) {
        JS_FreeValue(ctx, ab);
        return 0;
    }
    data = JS_GetArrayBuffer(ctx, &size, ab);
    if (!data || byte_off + byte_len > size || bpe != 1) {
        JS_FreeValue(ctx, ab);
        return 0;
    }
    *out = data + byte_off;
    *out_size = byte_len;
    JS_FreeValue(ctx, ab);
    return 1;
}

static JSValue js_analyser_get_fftSize(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    return JS_NewInt32(ctx, minirend_audio_analyser_fft_size(w->n));
}

static JSValue js_analyser_set_fftSize(JSContext* ctx, JSValueConst this_val, JSValueConst v) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    int32_t fft = 0;
    if (JS_ToInt32(ctx, &fft, v) != 0) return JS_ThrowTypeError(ctx, "invalid fftSize");
    minirend_audio_analyser_set_fft_size(w->n, fft);
    return JS_UNDEFINED;
}

static JSValue js_analyser_get_frequencyBinCount(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    return JS_NewInt32(ctx, minirend_audio_analyser_frequency_bin_count(w->n));
}

static JSValue js_analyser_getByteTimeDomainData(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "array required");
    uint8_t* dst = NULL; size_t dst_len = 0;
    if (!get_uint8array_bytes(ctx, argv[0], &dst, &dst_len)) return JS_ThrowTypeError(ctx, "expected Uint8Array");
    int src_len = 0;
    uint8_t* src = minirend_audio_analyser_get_byte_time_domain(w->n, &src_len);
    if (!src || src_len <= 0) return JS_UNDEFINED;
    size_t n = (dst_len < (size_t)src_len) ? dst_len : (size_t)src_len;
    memcpy(dst, src, n);
    return JS_UNDEFINED;
}

static JSValue js_analyser_getByteFrequencyData(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "array required");
    uint8_t* dst = NULL; size_t dst_len = 0;
    if (!get_uint8array_bytes(ctx, argv[0], &dst, &dst_len)) return JS_ThrowTypeError(ctx, "expected Uint8Array");
    int src_len = 0;
    uint8_t* src = minirend_audio_analyser_get_byte_frequency(w->n, &src_len);
    if (!src || src_len <= 0) return JS_UNDEFINED;
    size_t n = (dst_len < (size_t)src_len) ? dst_len : (size_t)src_len;
    memcpy(dst, src, n);
    return JS_UNDEFINED;
}

/* =========================
 * AudioContext
 * ========================= */

static JSValue js_audio_ctx_ctor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    MinirendAudioEngine* e = minirend_audio_engine_get();
    AudioContextWrap* w = (AudioContextWrap*)calloc(1, sizeof(*w));
    if (!w) return JS_EXCEPTION;
    w->e = e;

    JSValue obj = JS_NewObjectClass(ctx, js_audio_ctx_class_id);
    if (JS_IsException(obj)) {
        free(w);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, w);
    JS_SetPrototype(ctx, obj, audio_ctx_proto);
    (void)new_target;
    return obj;
}

static JSValue js_audio_ctx_get_sampleRate(JSContext* ctx, JSValueConst this_val) {
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_UNDEFINED;
    return JS_NewInt32(ctx, minirend_audio_engine_sample_rate(w->e));
}

static JSValue js_audio_ctx_get_currentTime(JSContext* ctx, JSValueConst this_val) {
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_UNDEFINED;
    return JS_NewFloat64(ctx, minirend_audio_engine_current_time(w->e));
}

static JSValue js_audio_ctx_get_state(JSContext* ctx, JSValueConst this_val) {
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_UNDEFINED;
    /* mirror our engine flags */
    JSValue closed = JS_GetPropertyStr(ctx, this_val, "__closed");
    if (JS_ToBool(ctx, closed)) {
        JS_FreeValue(ctx, closed);
        return JS_NewString(ctx, "closed");
    }
    JS_FreeValue(ctx, closed);
    JSValue running = JS_GetPropertyStr(ctx, this_val, "__running");
    if (JS_ToBool(ctx, running)) {
        JS_FreeValue(ctx, running);
        return JS_NewString(ctx, "running");
    }
    JS_FreeValue(ctx, running);
    return JS_NewString(ctx, "suspended");
}

static JSValue promise_resolve_undefined(JSContext* ctx) {
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, (JSValueConst[]){ JS_UNDEFINED });
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

static JSValue js_audio_ctx_resume(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    if (!minirend_audio_engine_resume(w->e)) return JS_ThrowInternalError(ctx, "audio resume failed");
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__running", JS_NewBool(ctx, 1));
    return promise_resolve_undefined(ctx);
}

static JSValue js_audio_ctx_suspend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    minirend_audio_engine_suspend(w->e);
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__running", JS_NewBool(ctx, 0));
    return promise_resolve_undefined(ctx);
}

static JSValue js_audio_ctx_close(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    minirend_audio_engine_close(w->e);
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__running", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__closed", JS_NewBool(ctx, 1));
    return promise_resolve_undefined(ctx);
}

static JSValue js_audio_ctx_get_destination(JSContext* ctx, JSValueConst this_val) {
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_UNDEFINED;
    MinirendAudioNode* dst = minirend_audio_engine_destination(w->e);
    if (!dst) return JS_UNDEFINED;
    minirend_audio_node_retain(dst);
    return make_audio_node(ctx, w->e, dst);
}

static JSValue js_audio_ctx_get_listener(JSContext* ctx, JSValueConst this_val) {
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_UNDEFINED;

    JSValue cached = JS_GetPropertyStr(ctx, this_val, "__listenerRef");
    if (!JS_IsUndefined(cached) && !JS_IsNull(cached)) {
        return cached; /* caller owns */
    }
    JS_FreeValue(ctx, cached);

    JSValue listener = JS_NewObject(ctx);
    if (JS_IsException(listener)) return JS_EXCEPTION;

    /* Attach AudioParams; retain destination node as an owner anchor */
    MinirendAudioNode* owner = minirend_audio_engine_destination(w->e);
    if (owner) minirend_audio_node_retain(owner);

    if (owner) {
        JS_SetPropertyStr(ctx, listener, "positionX", make_audio_param(ctx, w->e, owner, minirend_audio_listener_position_x(w->e)));
        JS_SetPropertyStr(ctx, listener, "positionY", make_audio_param(ctx, w->e, owner, minirend_audio_listener_position_y(w->e)));
        JS_SetPropertyStr(ctx, listener, "positionZ", make_audio_param(ctx, w->e, owner, minirend_audio_listener_position_z(w->e)));
        JS_SetPropertyStr(ctx, listener, "forwardX", make_audio_param(ctx, w->e, owner, minirend_audio_listener_forward_x(w->e)));
        JS_SetPropertyStr(ctx, listener, "forwardY", make_audio_param(ctx, w->e, owner, minirend_audio_listener_forward_y(w->e)));
        JS_SetPropertyStr(ctx, listener, "forwardZ", make_audio_param(ctx, w->e, owner, minirend_audio_listener_forward_z(w->e)));
        JS_SetPropertyStr(ctx, listener, "upX", make_audio_param(ctx, w->e, owner, minirend_audio_listener_up_x(w->e)));
        JS_SetPropertyStr(ctx, listener, "upY", make_audio_param(ctx, w->e, owner, minirend_audio_listener_up_y(w->e)));
        JS_SetPropertyStr(ctx, listener, "upZ", make_audio_param(ctx, w->e, owner, minirend_audio_listener_up_z(w->e)));
        minirend_audio_node_release(w->e, owner);
    }

    JS_SetPropertyStr(ctx, (JSValue)this_val, "__listenerRef", JS_DupValue(ctx, listener));
    return listener;
}

static JSValue js_audio_ctx_createGain(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    MinirendAudioNode* n = minirend_audio_node_create(w->e, MINIREND_AUDIO_NODE_GAIN);
    if (!n) return JS_ThrowInternalError(ctx, "out of memory");
    JSValue obj = make_audio_node(ctx, w->e, n);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "gain"),
        JS_NewCFunction2(ctx, js_gain_get_gain, "get gain", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    return obj;
}

static JSValue js_audio_ctx_createOscillator(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    MinirendAudioNode* n = minirend_audio_node_create(w->e, MINIREND_AUDIO_NODE_OSCILLATOR);
    if (!n) return JS_ThrowInternalError(ctx, "out of memory");
    JSValue obj = make_audio_node(ctx, w->e, n);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "frequency"),
        JS_NewCFunction2(ctx, js_osc_get_frequency, "get frequency", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "detune"),
        JS_NewCFunction2(ctx, js_osc_get_detune, "get detune", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "type"),
        JS_NewCFunction2(ctx, js_osc_get_type, "get type", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, js_osc_set_type, "set type", 1, JS_CFUNC_setter, 0),
        JS_PROP_CONFIGURABLE);
    JS_SetPropertyStr(ctx, obj, "__oscType", JS_NewString(ctx, "sine"));
    JS_SetPropertyStr(ctx, obj, "start", JS_NewCFunction(ctx, js_osc_start, "start", 1));
    JS_SetPropertyStr(ctx, obj, "stop", JS_NewCFunction(ctx, js_osc_stop, "stop", 1));
    return obj;
}

/* Stub constructors for other nodes; fully wired in later todos */
static JSValue js_audio_ctx_createBiquadFilter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    MinirendAudioNode* n = minirend_audio_node_create(w->e, MINIREND_AUDIO_NODE_BIQUAD);
    if (!n) return JS_ThrowInternalError(ctx, "out of memory");
    JSValue obj = make_audio_node(ctx, w->e, n);

    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "frequency"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_biquad_get_frequency, "get frequency", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "Q"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_biquad_get_q, "get Q", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "gain"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_biquad_get_gain, "get gain", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "type"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_biquad_get_type, "get type", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, (JSCFunction*)js_biquad_set_type, "set type", 1, JS_CFUNC_setter, 0),
        JS_PROP_CONFIGURABLE);

    JS_SetPropertyStr(ctx, obj, "__biquadType", JS_NewString(ctx, "lowpass"));
    return obj;
}

static JSValue js_audio_ctx_createAnalyser(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    MinirendAudioNode* n = minirend_audio_node_create(w->e, MINIREND_AUDIO_NODE_ANALYSER);
    if (!n) return JS_ThrowInternalError(ctx, "out of memory");
    JSValue obj = make_audio_node(ctx, w->e, n);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "fftSize"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_analyser_get_fftSize, "get fftSize", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, (JSCFunction*)js_analyser_set_fftSize, "set fftSize", 1, JS_CFUNC_setter, 0),
        JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "frequencyBinCount"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_analyser_get_frequencyBinCount, "get frequencyBinCount", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_SetPropertyStr(ctx, obj, "getByteTimeDomainData",
        JS_NewCFunction(ctx, js_analyser_getByteTimeDomainData, "getByteTimeDomainData", 1));
    JS_SetPropertyStr(ctx, obj, "getByteFrequencyData",
        JS_NewCFunction(ctx, js_analyser_getByteFrequencyData, "getByteFrequencyData", 1));
    return obj;
}

static JSValue js_audio_ctx_createPanner(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    MinirendAudioNode* n = minirend_audio_node_create(w->e, MINIREND_AUDIO_NODE_PANNER);
    if (!n) return JS_ThrowInternalError(ctx, "out of memory");
    JSValue obj = make_audio_node(ctx, w->e, n);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "positionX"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_panner_get_positionX, "get positionX", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "positionY"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_panner_get_positionY, "get positionY", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "positionZ"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_panner_get_positionZ, "get positionZ", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);

    /* Basic WebAudio fields (stored, but only position currently affects output) */
    JS_SetPropertyStr(ctx, obj, "panningModel", JS_NewString(ctx, "equalpower"));
    JS_SetPropertyStr(ctx, obj, "distanceModel", JS_NewString(ctx, "inverse"));
    JS_SetPropertyStr(ctx, obj, "refDistance", JS_NewFloat64(ctx, 1.0));
    JS_SetPropertyStr(ctx, obj, "maxDistance", JS_NewFloat64(ctx, 10000.0));
    JS_SetPropertyStr(ctx, obj, "rolloffFactor", JS_NewFloat64(ctx, 1.0));
    JS_SetPropertyStr(ctx, obj, "coneInnerAngle", JS_NewFloat64(ctx, 360.0));
    JS_SetPropertyStr(ctx, obj, "coneOuterAngle", JS_NewFloat64(ctx, 360.0));
    JS_SetPropertyStr(ctx, obj, "coneOuterGain", JS_NewFloat64(ctx, 0.0));
    return obj;
}

/* =========================
 * AudioBufferSourceNode
 * ========================= */

static JSValue js_bufsrc_get_playbackRate(JSContext* ctx, JSValueConst this_val) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    MinirendAudioParam* p = minirend_audio_buffer_source_playback_rate(w->n);
    if (!p) return JS_UNDEFINED;
    return make_audio_param(ctx, w->e, w->n, p);
}

static JSValue js_bufsrc_get_buffer(JSContext* ctx, JSValueConst this_val) {
    return JS_GetPropertyStr(ctx, this_val, "__bufferRef");
}

static JSValue js_bufsrc_set_buffer(JSContext* ctx, JSValueConst this_val, JSValueConst v) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    if (JS_IsNull(v) || JS_IsUndefined(v)) {
        minirend_audio_buffer_source_set_buffer(w->n, NULL);
        JS_SetPropertyStr(ctx, (JSValue)this_val, "__bufferRef", JS_NULL);
        return JS_UNDEFINED;
    }
    AudioBufferWrap* bw = get_audio_buffer(ctx, v);
    if (!bw || !bw->b) return JS_ThrowTypeError(ctx, "expected AudioBuffer");
    minirend_audio_buffer_source_set_buffer(w->n, bw->b);
    /* Keep a JS reference alive to prevent GC/free while playing */
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__bufferRef", JS_DupValue(ctx, v));
    return JS_UNDEFINED;
}

static JSValue js_bufsrc_get_loop(JSContext* ctx, JSValueConst this_val) {
    JSValue v = JS_GetPropertyStr(ctx, this_val, "__loop");
    if (JS_IsUndefined(v)) return JS_NewBool(ctx, 0);
    return v;
}

static JSValue js_bufsrc_set_loop(JSContext* ctx, JSValueConst this_val, JSValueConst v) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    int b = JS_ToBool(ctx, v);
    JSValue ls = JS_GetPropertyStr(ctx, this_val, "__loopStart");
    JSValue le = JS_GetPropertyStr(ctx, this_val, "__loopEnd");
    double dls = 0.0, dle = 0.0;
    JS_ToFloat64(ctx, &dls, ls);
    JS_ToFloat64(ctx, &dle, le);
    JS_FreeValue(ctx, ls);
    JS_FreeValue(ctx, le);
    minirend_audio_buffer_source_set_loop(w->n, b != 0, dls, dle);
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__loop", JS_NewBool(ctx, b));
    return JS_UNDEFINED;
}

static JSValue js_bufsrc_get_loopStart(JSContext* ctx, JSValueConst this_val) {
    JSValue v = JS_GetPropertyStr(ctx, this_val, "__loopStart");
    if (JS_IsUndefined(v)) return JS_NewFloat64(ctx, 0.0);
    return v;
}

static JSValue js_bufsrc_set_loopStart(JSContext* ctx, JSValueConst this_val, JSValueConst v) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    double d = 0.0;
    if (JS_ToFloat64(ctx, &d, v) != 0) return JS_ThrowTypeError(ctx, "invalid loopStart");
    int loop = JS_ToBool(ctx, JS_GetPropertyStr(ctx, this_val, "__loop"));
    JSValue le = JS_GetPropertyStr(ctx, this_val, "__loopEnd");
    double dle = 0.0;
    JS_ToFloat64(ctx, &dle, le);
    JS_FreeValue(ctx, le);
    minirend_audio_buffer_source_set_loop(w->n, loop != 0, d, dle);
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__loopStart", JS_NewFloat64(ctx, d));
    return JS_UNDEFINED;
}

static JSValue js_bufsrc_get_loopEnd(JSContext* ctx, JSValueConst this_val) {
    JSValue v = JS_GetPropertyStr(ctx, this_val, "__loopEnd");
    if (JS_IsUndefined(v)) return JS_NewFloat64(ctx, 0.0);
    return v;
}

static JSValue js_bufsrc_set_loopEnd(JSContext* ctx, JSValueConst this_val, JSValueConst v) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_UNDEFINED;
    double d = 0.0;
    if (JS_ToFloat64(ctx, &d, v) != 0) return JS_ThrowTypeError(ctx, "invalid loopEnd");
    int loop = JS_ToBool(ctx, JS_GetPropertyStr(ctx, this_val, "__loop"));
    JSValue ls = JS_GetPropertyStr(ctx, this_val, "__loopStart");
    double dls = 0.0;
    JS_ToFloat64(ctx, &dls, ls);
    JS_FreeValue(ctx, ls);
    minirend_audio_buffer_source_set_loop(w->n, loop != 0, dls, d);
    JS_SetPropertyStr(ctx, (JSValue)this_val, "__loopEnd", JS_NewFloat64(ctx, d));
    return JS_UNDEFINED;
}

static JSValue js_bufsrc_start(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_EXCEPTION;
    double when = minirend_audio_engine_current_time(w->e);
    double offset = 0.0;
    double duration = 0.0;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToFloat64(ctx, &when, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid when");
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_ToFloat64(ctx, &offset, argv[1]) != 0) return JS_ThrowTypeError(ctx, "invalid offset");
    }
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        if (JS_ToFloat64(ctx, &duration, argv[2]) != 0) return JS_ThrowTypeError(ctx, "invalid duration");
    }
    minirend_audio_buffer_source_start(w->e, w->n, when, offset, duration);
    return JS_UNDEFINED;
}

static JSValue js_bufsrc_stop(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioNodeWrap* w = get_audio_node(ctx, this_val);
    if (!w || !w->n) return JS_EXCEPTION;
    double when = minirend_audio_engine_current_time(w->e);
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToFloat64(ctx, &when, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid when");
    }
    minirend_audio_buffer_source_stop(w->e, w->n, when);
    return JS_UNDEFINED;
}

static JSValue js_audio_ctx_createBufferSource(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)argc; (void)argv;
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    MinirendAudioNode* n = minirend_audio_node_create(w->e, MINIREND_AUDIO_NODE_BUFFER_SOURCE);
    if (!n) return JS_ThrowInternalError(ctx, "out of memory");
    JSValue obj = make_audio_node(ctx, w->e, n);

    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "playbackRate"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_get_playbackRate, "get playbackRate", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "buffer"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_get_buffer, "get buffer", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_set_buffer, "set buffer", 1, JS_CFUNC_setter, 0),
        JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "loop"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_get_loop, "get loop", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_set_loop, "set loop", 1, JS_CFUNC_setter, 0),
        JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "loopStart"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_get_loopStart, "get loopStart", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_set_loopStart, "set loopStart", 1, JS_CFUNC_setter, 0),
        JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, obj, JS_NewAtom(ctx, "loopEnd"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_get_loopEnd, "get loopEnd", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, (JSCFunction*)js_bufsrc_set_loopEnd, "set loopEnd", 1, JS_CFUNC_setter, 0),
        JS_PROP_CONFIGURABLE);

    JS_SetPropertyStr(ctx, obj, "__bufferRef", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "__loop", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "__loopStart", JS_NewFloat64(ctx, 0.0));
    JS_SetPropertyStr(ctx, obj, "__loopEnd", JS_NewFloat64(ctx, 0.0));

    JS_SetPropertyStr(ctx, obj, "start", JS_NewCFunction(ctx, js_bufsrc_start, "start", 3));
    JS_SetPropertyStr(ctx, obj, "stop", JS_NewCFunction(ctx, js_bufsrc_stop, "stop", 1));
    return obj;
}

/* =========================
 * AudioBuffer methods
 * ========================= */

static JSValue js_audio_buffer_get_sampleRate(JSContext* ctx, JSValueConst this_val) {
    AudioBufferWrap* w = get_audio_buffer(ctx, this_val);
    if (!w || !w->b) return JS_UNDEFINED;
    return JS_NewInt32(ctx, minirend_audio_buffer_sample_rate(w->b));
}

static JSValue js_audio_buffer_get_length(JSContext* ctx, JSValueConst this_val) {
    AudioBufferWrap* w = get_audio_buffer(ctx, this_val);
    if (!w || !w->b) return JS_UNDEFINED;
    return JS_NewInt32(ctx, minirend_audio_buffer_length_frames(w->b));
}

static JSValue js_audio_buffer_get_numberOfChannels(JSContext* ctx, JSValueConst this_val) {
    AudioBufferWrap* w = get_audio_buffer(ctx, this_val);
    if (!w || !w->b) return JS_UNDEFINED;
    return JS_NewInt32(ctx, minirend_audio_buffer_channels(w->b));
}

static JSValue js_audio_buffer_get_duration(JSContext* ctx, JSValueConst this_val) {
    AudioBufferWrap* w = get_audio_buffer(ctx, this_val);
    if (!w || !w->b) return JS_UNDEFINED;
    int sr = minirend_audio_buffer_sample_rate(w->b);
    int frames = minirend_audio_buffer_length_frames(w->b);
    if (sr <= 0) return JS_NewFloat64(ctx, 0.0);
    return JS_NewFloat64(ctx, (double)frames / (double)sr);
}

static JSValue js_audio_buffer_getChannelData(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioBufferWrap* w = get_audio_buffer(ctx, this_val);
    if (!w || !w->b) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "channel required");
    int32_t ch = 0;
    if (JS_ToInt32(ctx, &ch, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid channel");
    int channels = minirend_audio_buffer_channels(w->b);
    int frames = minirend_audio_buffer_length_frames(w->b);
    if (ch < 0 || ch >= channels) return JS_ThrowRangeError(ctx, "channel out of range");

    float* tmp = (float*)malloc((size_t)frames * sizeof(float));
    if (!tmp) return JS_ThrowInternalError(ctx, "out of memory");
    float* src = minirend_audio_buffer_data(w->b);
    for (int i = 0; i < frames; i++) {
        tmp[i] = src[i * channels + ch];
    }
    JSValue ab = JS_NewArrayBufferCopy(ctx, (const uint8_t*)tmp, (size_t)frames * sizeof(float));
    free(tmp);
    if (JS_IsException(ab)) return JS_EXCEPTION;

    JSValue args[3];
    args[0] = ab;
    args[1] = JS_NewInt32(ctx, 0);
    args[2] = JS_NewInt32(ctx, frames);
    JSValue ta = JS_NewTypedArray(ctx, 3, (JSValueConst*)args, JS_TYPED_ARRAY_FLOAT32);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[2]);
    JS_FreeValue(ctx, ab);
    return ta;
}

/* =========================
 * AudioContext: buffer APIs
 * ========================= */

static JSValue js_audio_ctx_createBuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    if (argc < 3) return JS_ThrowTypeError(ctx, "numChannels,length,sampleRate required");
    int32_t ch = 0, length = 0, sr = 0;
    if (JS_ToInt32(ctx, &ch, argv[0]) != 0) return JS_ThrowTypeError(ctx, "invalid numChannels");
    if (JS_ToInt32(ctx, &length, argv[1]) != 0) return JS_ThrowTypeError(ctx, "invalid length");
    if (JS_ToInt32(ctx, &sr, argv[2]) != 0) return JS_ThrowTypeError(ctx, "invalid sampleRate");
    if (ch <= 0 || length < 0 || sr <= 0) return JS_ThrowRangeError(ctx, "invalid args");

    MinirendAudioBuffer* b = minirend_audio_buffer_create(ch, sr, length);
    if (!b) return JS_ThrowInternalError(ctx, "out of memory");
    return make_audio_buffer(ctx, b);
}

static int get_arraybuffer_bytes(JSContext* ctx, JSValueConst v, uint8_t** out, size_t* out_size) {
    size_t size = 0;
    uint8_t* data = JS_GetArrayBuffer(ctx, &size, v);
    if (data) {
        *out = data;
        *out_size = size;
        return 1;
    }
    /* maybe typed array view */
    size_t byte_off = 0, byte_len = 0, bpe = 0;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, v, &byte_off, &byte_len, &bpe);
    if (!JS_IsException(ab) && !JS_IsNull(ab) && !JS_IsUndefined(ab)) {
        data = JS_GetArrayBuffer(ctx, &size, ab);
        if (data && byte_off + byte_len <= size) {
            *out = data + byte_off;
            *out_size = byte_len;
            JS_FreeValue(ctx, ab);
            return 1;
        }
    }
    JS_FreeValue(ctx, ab);
    return 0;
}

static JSValue js_audio_ctx_decodeAudioData(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    AudioContextWrap* w = get_audio_ctx(ctx, this_val);
    if (!w || !w->e) return JS_EXCEPTION;
    if (argc < 1) return JS_ThrowTypeError(ctx, "arrayBuffer required");

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);

    uint8_t* bytes = NULL;
    size_t   nbytes = 0;
    if (!get_arraybuffer_bytes(ctx, argv[0], &bytes, &nbytes)) {
        JSValue err = JS_NewString(ctx, "decodeAudioData: expected ArrayBuffer/TypedArray");
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, err);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        return promise;
    }

    MinirendAudioBuffer* b = minirend_audio_decode_wav(bytes, nbytes);
    if (!b) {
        JSValue err = JS_NewString(ctx, "decodeAudioData: unsupported/invalid WAV");
        JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &err);
        JS_FreeValue(ctx, err);
    } else {
        JSValue buf_obj = make_audio_buffer(ctx, b);
        JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &buf_obj);
        JS_FreeValue(ctx, buf_obj);
    }

    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/* Base AudioNode methods installed on prototype */
static void define_audio_node_methods(JSContext* ctx, JSValue proto) {
    JS_SetPropertyStr(ctx, proto, "connect", JS_NewCFunction(ctx, js_audio_node_connect, "connect", 1));
    JS_SetPropertyStr(ctx, proto, "disconnect", JS_NewCFunction(ctx, js_audio_node_disconnect, "disconnect", 1));
}

static void define_audio_param_methods(JSContext* ctx, JSValue proto) {
    JS_SetPropertyStr(ctx, proto, "setValueAtTime", JS_NewCFunction(ctx, js_audio_param_setValueAtTime, "setValueAtTime", 2));
    JS_SetPropertyStr(ctx, proto, "linearRampToValueAtTime", JS_NewCFunction(ctx, js_audio_param_linearRampToValueAtTime, "linearRampToValueAtTime", 2));
    JS_SetPropertyStr(ctx, proto, "exponentialRampToValueAtTime", JS_NewCFunction(ctx, js_audio_param_exponentialRampToValueAtTime, "exponentialRampToValueAtTime", 2));
    JS_SetPropertyStr(ctx, proto, "setTargetAtTime", JS_NewCFunction(ctx, js_audio_param_setTargetAtTime, "setTargetAtTime", 3));
    JS_DefinePropertyGetSet(ctx, proto, JS_NewAtom(ctx, "value"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_param_get_value, "get value", 0, JS_CFUNC_getter, 0),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_param_set_value, "set value", 1, JS_CFUNC_setter, 0),
        JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, proto, JS_NewAtom(ctx, "defaultValue"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_param_get_defaultValue, "get defaultValue", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, proto, JS_NewAtom(ctx, "minValue"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_param_get_minValue, "get minValue", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, proto, JS_NewAtom(ctx, "maxValue"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_param_get_maxValue, "get maxValue", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
}

static void register_audio_classes(JSContext* ctx) {
    static int registered = 0;
    if (registered) return;
    registered = 1;
    JSRuntime* rt = JS_GetRuntime(ctx);
    JS_NewClassID(&js_audio_ctx_class_id);
    JS_NewClassID(&js_audio_node_class_id);
    JS_NewClassID(&js_audio_param_class_id);
    JS_NewClassID(&js_audio_buffer_class_id);

    JS_NewClass(rt, js_audio_ctx_class_id, &js_audio_ctx_class);
    JS_NewClass(rt, js_audio_node_class_id, &js_audio_node_class);
    JS_NewClass(rt, js_audio_param_class_id, &js_audio_param_class);
    JS_NewClass(rt, js_audio_buffer_class_id, &js_audio_buffer_class);
}

void minirend_audio_register(JSContext* ctx) {
    register_audio_classes(ctx);

    audio_ctx_proto = JS_NewObject(ctx);
    audio_node_proto = JS_NewObject(ctx);
    audio_param_proto = JS_NewObject(ctx);
    audio_buffer_proto = JS_NewObject(ctx);

    /* AudioNode base */
    define_audio_node_methods(ctx, audio_node_proto);

    /* AudioParam */
    define_audio_param_methods(ctx, audio_param_proto);

    /* AudioBuffer */
    JS_DefinePropertyGetSet(ctx, audio_buffer_proto, JS_NewAtom(ctx, "sampleRate"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_buffer_get_sampleRate, "get sampleRate", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, audio_buffer_proto, JS_NewAtom(ctx, "length"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_buffer_get_length, "get length", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, audio_buffer_proto, JS_NewAtom(ctx, "duration"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_buffer_get_duration, "get duration", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, audio_buffer_proto, JS_NewAtom(ctx, "numberOfChannels"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_buffer_get_numberOfChannels, "get numberOfChannels", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_SetPropertyStr(ctx, audio_buffer_proto, "getChannelData",
        JS_NewCFunction(ctx, js_audio_buffer_getChannelData, "getChannelData", 1));

    /* AudioContext prototype */
    JS_DefinePropertyGetSet(ctx, audio_ctx_proto, JS_NewAtom(ctx, "sampleRate"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_ctx_get_sampleRate, "get sampleRate", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, audio_ctx_proto, JS_NewAtom(ctx, "currentTime"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_ctx_get_currentTime, "get currentTime", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, audio_ctx_proto, JS_NewAtom(ctx, "state"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_ctx_get_state, "get state", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, audio_ctx_proto, JS_NewAtom(ctx, "destination"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_ctx_get_destination, "get destination", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);
    JS_DefinePropertyGetSet(ctx, audio_ctx_proto, JS_NewAtom(ctx, "listener"),
        JS_NewCFunction2(ctx, (JSCFunction*)js_audio_ctx_get_listener, "get listener", 0, JS_CFUNC_getter, 0),
        JS_UNDEFINED, JS_PROP_CONFIGURABLE);

    JS_SetPropertyStr(ctx, audio_ctx_proto, "resume", JS_NewCFunction(ctx, js_audio_ctx_resume, "resume", 0));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "suspend", JS_NewCFunction(ctx, js_audio_ctx_suspend, "suspend", 0));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "close", JS_NewCFunction(ctx, js_audio_ctx_close, "close", 0));

    JS_SetPropertyStr(ctx, audio_ctx_proto, "createGain", JS_NewCFunction(ctx, js_audio_ctx_createGain, "createGain", 0));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "createOscillator", JS_NewCFunction(ctx, js_audio_ctx_createOscillator, "createOscillator", 0));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "createBufferSource", JS_NewCFunction(ctx, js_audio_ctx_createBufferSource, "createBufferSource", 0));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "createBiquadFilter", JS_NewCFunction(ctx, js_audio_ctx_createBiquadFilter, "createBiquadFilter", 0));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "createAnalyser", JS_NewCFunction(ctx, js_audio_ctx_createAnalyser, "createAnalyser", 0));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "createPanner", JS_NewCFunction(ctx, js_audio_ctx_createPanner, "createPanner", 0));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "createBuffer", JS_NewCFunction(ctx, js_audio_ctx_createBuffer, "createBuffer", 3));
    JS_SetPropertyStr(ctx, audio_ctx_proto, "decodeAudioData", JS_NewCFunction(ctx, js_audio_ctx_decodeAudioData, "decodeAudioData", 1));

    /* Install global constructors */
    JSValue global = JS_GetGlobalObject(ctx);

    JSValue ctor = JS_NewCFunction2(ctx, js_audio_ctx_ctor, "AudioContext", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, audio_ctx_proto);
    JS_SetPropertyStr(ctx, global, "AudioContext", ctor);
    JS_SetPropertyStr(ctx, global, "webkitAudioContext", JS_DupValue(ctx, ctor));

    JS_FreeValue(ctx, global);
}

void minirend_audio_tick(void) {
    minirend_audio_engine_tick();
}

void minirend_audio_shutdown(void) {
    minirend_audio_engine_shutdown();
}


