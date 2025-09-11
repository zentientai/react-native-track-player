#pragma once

#include <jni.h>
#include <jsi/jsi.h>
#include <string>

namespace rntp {

// Collections and value conversion
jobject createHashMap(JNIEnv* env);
void putInHashMap(JNIEnv* env, jobject map, const std::string& key, jobject value);
jobject jsiObjectToMap(JNIEnv* env, facebook::jsi::Runtime& rt, const facebook::jsi::Object& obj);
jobject jsiArrayToList(JNIEnv* env, facebook::jsi::Runtime& rt, const facebook::jsi::Array& arr);
jobject jsiValueToJava(JNIEnv* env, facebook::jsi::Runtime& rt, const facebook::jsi::Value& value);
jobject wrapValueInMap(JNIEnv* env, facebook::jsi::Runtime& rt, const facebook::jsi::Value& value);

// Error and string helpers
void handleJSIError(JNIEnv* env, jobject callbackGlobal, const std::string& error);
std::string extractJNIString(JNIEnv* env, jstring jStr);
facebook::jsi::String createJSIString(facebook::jsi::Runtime& rt, const std::string& str);

} // namespace rntp


