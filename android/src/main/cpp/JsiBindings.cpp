#include <jni.h>
#include <jsi/jsi.h>
#include <android/log.h>
#include <string>
#include <functional>
#include "JsiUtils.h"

using namespace facebook;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "RNTP", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RNTP", __VA_ARGS__)

static jsi::Runtime* globalRuntime = nullptr;
static JavaVM* gJvm = nullptr;

// Forward declarations
static JNIEnv* acquireJNIEnv(bool& didAttach);
static void releaseJNIEnv(bool didAttach);

// RAII wrapper for JNI environment management
class JNIEnvGuard {
private:
    JNIEnv* env_;
    bool didAttach_;
    
public:
    JNIEnvGuard() : env_(nullptr), didAttach_(false) {
        env_ = acquireJNIEnv(didAttach_);
    }
    
    ~JNIEnvGuard() {
        releaseJNIEnv(didAttach_);
    }
    
    JNIEnv* get() const { return env_; }
    bool isValid() const { return env_ != nullptr; }
    
    // Non-copyable
    JNIEnvGuard(const JNIEnvGuard&) = delete;
    JNIEnvGuard& operator=(const JNIEnvGuard&) = delete;
};

// Utility functions for JNI environment management
static JNIEnv* acquireJNIEnv(bool& didAttach) {
    JNIEnv* env = nullptr;
    didAttach = false;
    if (gJvm && gJvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (gJvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            didAttach = true;
        }
    }
    return env;
}

static void releaseJNIEnv(bool didAttach) {
    if (didAttach && gJvm) {
        gJvm->DetachCurrentThread();
    }
}

// Template function for creating host functions with common error handling
template<typename Callback>
static jsi::Function createHostFunction(jsi::Runtime& rt, const std::string& name, 
                                       jobject callbackGlobal, Callback callback) {
    return jsi::Function::createFromHostFunction(
        rt, jsi::PropNameID::forAscii(rt, name.c_str()), 1,
        [callbackGlobal, callback, name](jsi::Runtime& runtime, const jsi::Value&, 
                                 const jsi::Value* args, size_t count) -> jsi::Value {
            JNIEnvGuard guard;
            if (!guard.isValid()) {
                LOGE("Failed to get JNIEnv in %s", name.c_str());
                return jsi::Value::undefined();
            }
            
            try {
                callback(guard.get(), runtime, args, count);
            } catch (...) {
                LOGE("Exception in %s handler", name.c_str());
            }
            
            return jsi::Value::undefined();
        }
    );
}

// JNI: call JS function that returns a promise and resolve/reject via callback
extern "C"
JNIEXPORT void JNICALL
Java_com_doublesymmetry_trackplayer_JsiBridge_nativeCallJS(
    JNIEnv* env, jobject thiz, jstring jFnName, jstring jInput, jobject jCallback) {

    if (!globalRuntime) {
        LOGI("JSI Runtime not initialized");
        return;
    }

    jobject callbackGlobal = env->NewGlobalRef(jCallback);
    std::string fnName = rntp::extractJNIString(env, jFnName);
    std::string input = rntp::extractJNIString(env, jInput);

    auto rt = globalRuntime;

    try {
        // Use JS helper: global.__rntpCall(fnName, arg, resolve, reject) which handles then/catch in JS
        jsi::Value callVal = rt->global().getProperty(*rt, "__rntpCall");
        if (!callVal.isObject() || !callVal.getObject(*rt).isFunction(*rt)) {
            LOGI("CPP: __rntpCall not found or not a function");
            JNIEnvGuard guard;
            if (guard.isValid()) {
                rntp::handleJSIError(guard.get(), callbackGlobal, "__rntpCall not available");
            }
            env->DeleteGlobalRef(callbackGlobal);
            return;
        }
        jsi::Function callFn = callVal.getObject(*rt).asFunction(*rt);

        // Create resolve handler using template
        auto resolveFn = createHostFunction(*rt, "resolveHandler", callbackGlobal,
            [callbackGlobal](JNIEnv* envCb, jsi::Runtime& runtime, const jsi::Value* args, size_t count) {
                jclass cls = envCb->GetObjectClass(callbackGlobal);
                jmethodID onResolve = envCb->GetMethodID(cls, "onResolve", "(Ljava/util/Map;)V");
                jobject argMap = nullptr;

                if (count > 0) {
                    const jsi::Value& v = args[0];
                    if (v.isObject()) {
                        jsi::Object o = v.getObject(runtime);
                        if (!o.isArray(runtime)) {
                            argMap = rntp::jsiObjectToMap(envCb, runtime, o);
                        }
                    }
                    if (!argMap) {
                        // Wrap non-object (or arrays) into a map under key "value"
                        argMap = rntp::wrapValueInMap(envCb, runtime, v);
                    }
                }

                envCb->CallVoidMethod(callbackGlobal, onResolve, argMap);
                if (argMap) envCb->DeleteLocalRef(argMap);
                envCb->DeleteGlobalRef(callbackGlobal);
            }
        );

        // Create reject handler using template
        auto rejectFn = createHostFunction(*rt, "rejectHandler", callbackGlobal,
            [callbackGlobal](JNIEnv* envCb, jsi::Runtime& runtime, const jsi::Value* args, size_t count) {
                std::string err = count > 0 ? args[0].toString(runtime).utf8(runtime) : "Unknown error";
                rntp::handleJSIError(envCb, callbackGlobal, err);
            }
        );

        // Let JS perform Promise resolution; we just pass handlers
        callFn.call(
            *rt,
            rntp::createJSIString(*rt, fnName),
            rntp::createJSIString(*rt, input),
            resolveFn,
            rejectFn
        );
    } catch (const std::exception& e) {
        LOGE("CPP: Exception: %s", e.what());
        JNIEnvGuard guard;
        if (guard.isValid()) {
            rntp::handleJSIError(guard.get(), callbackGlobal, e.what());
        }
        env->DeleteGlobalRef(callbackGlobal);
    }

    // Global ref is deleted in resolve/reject handlers
}

// JNI: Initialize JSI runtime
extern "C"
JNIEXPORT void JNICALL
Java_com_doublesymmetry_trackplayer_JsiBridge_initializeJSI(
    JNIEnv* env, jobject thiz, jlong runtimePtr) {
    if (runtimePtr == 0) {
        return;
    }
    globalRuntime = reinterpret_cast<jsi::Runtime*>(runtimePtr);
}

// Cache JavaVM for later thread attachment from JSI callbacks
jint JNI_OnLoad(JavaVM* vm, void*) {
    gJvm = vm;
    return JNI_VERSION_1_6;
}
