#include "JsiUtils.h"
#include <android/log.h>

using namespace facebook;

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RNTP", __VA_ARGS__)

namespace rntp {

jobject createHashMap(JNIEnv* env) {
    jclass hashMapClass = env->FindClass("java/util/HashMap");
    jmethodID hashMapCtor = env->GetMethodID(hashMapClass, "<init>", "()V");
    return env->NewObject(hashMapClass, hashMapCtor);
}

void putInHashMap(JNIEnv* env, jobject map, const std::string& key, jobject value) {
    jclass hashMapClass = env->FindClass("java/util/HashMap");
    jmethodID putMethod = env->GetMethodID(hashMapClass, "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    jstring jKey = env->NewStringUTF(key.c_str());
    env->CallObjectMethod(map, putMethod, jKey, value);
    env->DeleteLocalRef(jKey);
    if (value) env->DeleteLocalRef(value);
}

jobject jsiArrayToList(JNIEnv* env, jsi::Runtime& rt, const jsi::Array& arr) {
    jclass arrayListClass = env->FindClass("java/util/ArrayList");
    jmethodID arrayListCtor = env->GetMethodID(arrayListClass, "<init>", "()V");
    jmethodID addMethod = env->GetMethodID(arrayListClass, "add", "(Ljava/lang/Object;)Z");
    jobject list = env->NewObject(arrayListClass, arrayListCtor);

    for (size_t i = 0; i < arr.size(rt); i++) {
        jobject elem = jsiValueToJava(env, rt, arr.getValueAtIndex(rt, i));
        env->CallBooleanMethod(list, addMethod, elem);
        if (elem) env->DeleteLocalRef(elem);
    }
    return list;
}

jobject jsiObjectToMap(JNIEnv* env, jsi::Runtime& rt, const jsi::Object& obj) {
    jobject map = createHashMap(env);

    jsi::Array keys = obj.getPropertyNames(rt);
    for (size_t i = 0; i < keys.size(rt); i++) {
        std::string key = keys.getValueAtIndex(rt, i).toString(rt).utf8(rt);
        jsi::Value val = obj.getProperty(rt, key.c_str());
        jobject jVal = jsiValueToJava(env, rt, val);
        putInHashMap(env, map, key, jVal);
    }
    return map;
}

jobject jsiValueToJava(JNIEnv* env, jsi::Runtime& rt, const jsi::Value& value) {
    if (value.isUndefined() || value.isNull()) return nullptr;

    if (value.isBool()) {
        jclass booleanClass = env->FindClass("java/lang/Boolean");
        jmethodID ctor = env->GetMethodID(booleanClass, "<init>", "(Z)V");
        return env->NewObject(booleanClass, ctor, value.getBool());
    }

    if (value.isNumber()) {
        jclass doubleClass = env->FindClass("java/lang/Double");
        jmethodID ctor = env->GetMethodID(doubleClass, "<init>", "(D)V");
        return env->NewObject(doubleClass, ctor, value.asNumber());
    }

    if (value.isString()) {
        return env->NewStringUTF(value.getString(rt).utf8(rt).c_str());
    }

    if (value.isObject()) {
        jsi::Object obj = value.getObject(rt);
        if (obj.isArray(rt)) return jsiArrayToList(env, rt, obj.asArray(rt));
        else return jsiObjectToMap(env, rt, obj);
    }

    return nullptr;
}

jobject wrapValueInMap(JNIEnv* env, jsi::Runtime& rt, const jsi::Value& value) {
    jobject map = createHashMap(env);
    jobject jVal = jsiValueToJava(env, rt, value);
    putInHashMap(env, map, "value", jVal);
    return map;
}

void handleJSIError(JNIEnv* env, jobject callbackGlobal, const std::string& error) {
    jclass cls = env->GetObjectClass(callbackGlobal);
    jmethodID onReject = env->GetMethodID(cls, "onReject", "(Ljava/lang/String;)V");
    jstring jErr = env->NewStringUTF(error.c_str());
    env->CallVoidMethod(callbackGlobal, onReject, jErr);
    env->DeleteLocalRef(jErr);
}

std::string extractJNIString(JNIEnv* env, jstring jStr) {
    if (!jStr) return "";
    const char* cStr = env->GetStringUTFChars(jStr, nullptr);
    std::string result(cStr);
    env->ReleaseStringUTFChars(jStr, cStr);
    return result;
}

jsi::String createJSIString(jsi::Runtime& rt, const std::string& str) {
    return jsi::String::createFromUtf8(rt, str.c_str());
}

} // namespace rntp


