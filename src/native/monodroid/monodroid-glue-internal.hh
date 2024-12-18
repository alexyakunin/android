// Dear Emacs, this is a -*- C++ -*- header
#ifndef __MONODROID_GLUE_INTERNAL_H
#define __MONODROID_GLUE_INTERNAL_H

#include <string>
#include <string_view>

#include <jni.h>
#include "android-system.hh"
#include "osbridge.hh"
#include "timing.hh"
#include "cpp-util.hh"
#include "xxhash.hh"
#include "monodroid-dl.hh"

#include <mono/utils/mono-counters.h>
#include <mono/metadata/profiler.h>

// NDEBUG causes robin_map.h not to include <iostream> which, in turn, prevents indirect inclusion of <mutex>. <mutex>
// conflicts with our std::mutex definition in cppcompat.hh
#if !defined (NDEBUG)
#define NDEBUG
#define NDEBUG_UNDEFINE
#endif

// hush some compiler warnings
#if defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif // __clang__

#include <tsl/robin_map.h>

#if defined (__clang__)
#pragma clang diagnostic pop
#endif // __clang__

#if defined (NDEBUG_UNDEFINE)
#undef NDEBUG
#undef NDEBUG_UNDEFINE
#endif

//#include <mono/utils/mono-publib.h>
#include <mono/jit/mono-private-unstable.h>
#include <mono/metadata/mono-private-unstable.h>

// See https://github.com/dotnet/runtime/pull/67024
// See https://github.com/xamarin/xamarin-android/issues/6935
extern mono_bool mono_opt_aot_lazy_assembly_load;

namespace xamarin::android::internal
{
	// Values must be identical to those in src/Mono.Android/Android.Runtime/RuntimeNativeMethods.cs
	enum class TraceKind : uint32_t
	{
		Java    = 0x01,
		Managed = 0x02,
		Native  = 0x04,
		Signals = 0x08,
	};

	class MonodroidRuntime
	{
		using load_assemblies_context_type = MonoAssemblyLoadContextGCHandle;

#if defined (DEBUG)
		struct RuntimeOptions {
			bool debug = false;
			int loglevel = 0;
			int64_t timeout_time = 0;
			char *host = nullptr;
			uint16_t sdb_port = 0;
			uint16_t out_port = 0;
			bool server = false;
		};
#endif

		// Keep the enum values in sync with those in src/Mono.Android/AndroidRuntime/BoundExceptionType.cs
		enum class BoundExceptionType : uint8_t
		{
			System = 0x00,
			Java   = 0x01,
		};

		// NOTE: Keep this in sync with managed side in src/Mono.Android/Android.Runtime/JNIEnvInit.cs
		struct JnienvInitializeArgs {
			JavaVM         *javaVm;
			JNIEnv         *env;
			jobject         grefLoader;
			jmethodID       Loader_loadClass;
			jclass          grefClass;
			jmethodID       Class_forName;
			unsigned int    logCategories;
			int             version;
			int             grefGcThreshold;
			jobject         grefIGCUserPeer;
			int             isRunningOnDesktop;
			uint8_t         brokenExceptionTransitions;
			int             packageNamingPolicy;
			uint8_t         boundExceptionType;
			int             jniAddNativeMethodRegistrationAttributePresent;
			bool            jniRemappingInUse;
			bool            marshalMethodsEnabled;
		};

		using jnienv_initialize_fn = void (*) (JnienvInitializeArgs*);
		using jnienv_register_jni_natives_fn = void (*)(const jchar *typeName_ptr, int32_t typeName_len, jclass jniClass, const jchar *methods_ptr, int32_t methods_len);

	private:
		static constexpr std::string_view base_apk_name { "/base.apk" };
		static constexpr size_t SMALL_STRING_PARSE_BUFFER_LEN = 50uz;
		static constexpr bool is_running_on_desktop = false;

	public:
		static constexpr int XA_LOG_COUNTERS = MONO_COUNTER_JIT | MONO_COUNTER_METADATA | MONO_COUNTER_GC | MONO_COUNTER_GENERICS | MONO_COUNTER_INTERP;

	public:
		void Java_mono_android_Runtime_register (JNIEnv *env, jstring managedType, jclass nativeClass, jstring methods);
		void Java_mono_android_Runtime_initInternal (JNIEnv *env, jclass klass, jstring lang, jobjectArray runtimeApksJava,
		                                             jstring runtimeNativeLibDir, jobjectArray appDirs, jint localDateTimeOffset,
		                                             jobject loader, jobjectArray assembliesJava, jboolean isEmulator,
		                                             jboolean haveSplitApks);

		jint Java_JNI_OnLoad (JavaVM *vm, void *reserved);

		jclass get_java_class_System () const
		{
			return java_System;
		}

		jmethodID get_java_class_method_System_identityHashCode () const
		{
			return java_System_identityHashCode;
		}

		jclass get_java_class_TimeZone () const
		{
			return java_TimeZone;
		}

		void set_monodroid_gdb_wait (bool yes_no)
		{
			monodroid_gdb_wait = yes_no;
		}

		void propagate_uncaught_exception (JNIEnv *env, jobject javaThread, jthrowable javaException);
		char*	get_java_class_name_for_TypeManager (jclass klass);
		void log_traces (JNIEnv *env, TraceKind kind, const char *first_line) noexcept;

	private:
		static void mono_log_handler (const char *log_domain, const char *log_level, const char *message, mono_bool fatal, void *user_data);
		static void mono_log_standard_streams_handler (const char *str, mono_bool is_stdout);

		// A reference to unique_ptr is not the best practice ever, but it's faster this way
		void setup_mono_tracing (std::unique_ptr<char[]> const& mono_log_mask, bool have_log_assembly, bool have_log_gc);
		void install_logging_handlers ();

		unsigned int convert_dl_flags (int flags);

		static void  cleanup_runtime_config (MonovmRuntimeConfigArguments *args, void *user_data);

		template<typename TFunc>
		static void load_symbol (void *handle, const char *name, TFunc*& fnptr) noexcept
		{
			char *err = nullptr;
			void *symptr = MonodroidDl::monodroid_dlsym (handle, name, &err, nullptr);

			if (symptr == nullptr) {
				log_warn (LOG_DEFAULT, "Failed to load symbol '%s' library with handle %p. %s", name, handle, err == nullptr ? "Unknown error" : err);
				fnptr = nullptr;
				return;
			}

			fnptr = reinterpret_cast<TFunc*>(symptr);
		}

		void create_xdg_directory (jstring_wrapper& home, size_t home_len, std::string_view const& relative_path, std::string_view const& environment_variable_name) noexcept;
		void create_xdg_directories_and_environment (jstring_wrapper &homeDir);
		void lookup_bridge_info (MonoClass *klass, const OSBridge::MonoJavaGCBridgeType *type, OSBridge::MonoJavaGCBridgeInfo *info);
		void lookup_bridge_info (MonoImage *image, const OSBridge::MonoJavaGCBridgeType *type, OSBridge::MonoJavaGCBridgeInfo *info);
		void load_assembly (MonoDomain *domain, jstring_wrapper &assembly);
		void load_assembly (MonoAssemblyLoadContextGCHandle alc_handle, jstring_wrapper &assembly);
		void load_assemblies (load_assemblies_context_type ctx, bool preload, jstring_array_wrapper &assemblies);

		void set_debug_options ();
		void parse_gdb_options ();
		void mono_runtime_init (JNIEnv *env, dynamic_local_string<PROPERTY_VALUE_BUFFER_LEN>& runtime_args);
		void init_android_runtime (JNIEnv *env, jclass runtimeClass, jobject loader);
		void set_environment_variable_for_directory (const char *name, jstring_wrapper &value, bool createDirectory, mode_t mode);

		void set_environment_variable_for_directory (const char *name, jstring_wrapper &value)
		{
			set_environment_variable_for_directory (name, value, true, DEFAULT_DIRECTORY_MODE);
		}

		void set_environment_variable (const char *name, jstring_wrapper &value)
		{
			set_environment_variable_for_directory (name, value, false, 0);
		}

		static void monodroid_unhandled_exception (MonoObject *java_exception);
		MonoClass* get_android_runtime_class ();

		MonoDomain*	create_domain (JNIEnv *env, jstring_array_wrapper &runtimeApks, bool is_root_domain, bool have_split_apks);
		MonoDomain* create_and_initialize_domain (JNIEnv* env, jclass runtimeClass, jstring_array_wrapper &runtimeApks,
		                                          jstring_array_wrapper &assemblies, jobjectArray assembliesBytes, jstring_array_wrapper &assembliesPaths,
		                                          jobject loader, bool is_root_domain, bool force_preload_assemblies,
		                                          bool have_split_apks);

		void gather_bundled_assemblies (jstring_array_wrapper &runtimeApks, size_t *out_user_assemblies_count, bool have_split_apks);
		static bool should_register_file (const char *filename);
		void set_trace_options ();
		void set_profile_options ();

		void log_jit_event (MonoMethod *method, const char *event_name);
		static void jit_begin (MonoProfiler *prof, MonoMethod *method);
		static void jit_failed (MonoProfiler *prof, MonoMethod *method);
		static void jit_done (MonoProfiler *prof, MonoMethod *method, MonoJitInfo* jinfo);
		static void thread_start (MonoProfiler *prof, uintptr_t tid);
		static void thread_end (MonoProfiler *prof, uintptr_t tid);
#if !defined (RELEASE)
		static MonoReflectionType* typemap_java_to_managed (MonoString *java_type_name) noexcept;
		static const char* typemap_managed_to_java (MonoReflectionType *type, const uint8_t *mvid) noexcept;
#endif // !def RELEASE

		static void monodroid_debugger_unhandled_exception (MonoException *ex);

#if defined (RELEASE)
		static const char* get_method_name (uint32_t mono_image_index, uint32_t method_token) noexcept;
		static const char* get_class_name (uint32_t class_index) noexcept;

		template<bool NeedsLocking>
		static void get_function_pointer (uint32_t mono_image_index, uint32_t class_index, uint32_t method_token, void*& target_ptr) noexcept;
		static void get_function_pointer_at_startup (uint32_t mono_image_index, uint32_t class_token, uint32_t method_token, void*& target_ptr) noexcept;
		static void get_function_pointer_at_runtime (uint32_t mono_image_index, uint32_t class_token, uint32_t method_token, void*& target_ptr) noexcept;
#endif // def RELEASE

#if defined (DEBUG)
		void set_debug_env_vars (void);
		bool parse_runtime_args (dynamic_local_string<PROPERTY_VALUE_BUFFER_LEN> &runtime_args, RuntimeOptions *options);
		int monodroid_debug_connect (int sock, struct sockaddr_in addr);
		int monodroid_debug_accept (int sock, struct sockaddr_in addr);
#endif // DEBUG

#if !defined (RELEASE)
		static MonoAssembly* open_from_update_dir (MonoAssemblyName *aname, char **assemblies_path, void *user_data);
#endif
	private:
		MonoMethod         *registerType          = nullptr;
		volatile bool       monodroid_gdb_wait    = true;
		jclass              java_System;
		jmethodID           java_System_identityHashCode;
		jmethodID           Class_getName;
		jclass              java_TimeZone;
		timing_period       jit_time;
		FILE               *jit_log;
		MonoProfilerHandle  profiler_handle;

		/*
		 * If set, monodroid will spin in a loop until the debugger breaks the wait by
		 * clearing monodroid_gdb_wait.
		 */
		bool                wait_for_gdb;

		/* The context (mapping to a Mono AppDomain) that is currently selected as the
		 * active context from the point of view of Java. We cannot rely on the value
		 * of `mono_domain_get` for this as it's stored per-thread and we want to be
		 * able to switch our different contexts from different threads.
		 */
		int                 current_context_id = -1;

		jnienv_register_jni_natives_fn jnienv_register_jni_natives = nullptr;
		MonoAssemblyLoadContextGCHandle default_alc = nullptr;

		static MonoCoreRuntimeProperties monovm_core_properties;
		MonovmRuntimeConfigArguments  runtime_config_args;
	};
}
#endif
