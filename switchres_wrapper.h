#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#  ifdef MODULE_API_EXPORTS
#    define MODULE_API __declspec(dllexport)
#  else
#    define MODULE_API __declspec(dllimport)
#  endif
#else
#  define MODULE_API
#endif

// Instanciate a new switchres_manager object, read a switchres_ini file
MODULE_API void sr_init();
MODULE_API void sr_deinit();
MODULE_API unsigned char sr_get_mode(int*, int*, double*, unsigned char*);
MODULE_API unsigned char sr_switch_to_mode(int*, int*, double*, unsigned char*);
MODULE_API void sr_set_monitor(const char*);


MODULE_API void simple_test();
MODULE_API void simple_test_with_params(int width, int height, double refresh, unsigned char interlace, unsigned char rotate);



// Inspired by https://stackoverflow.com/a/1067684
typedef struct MODULE_API {
   void (*init)(void);
   void (*deinit)(void);
   unsigned char (*sr_get_mode)(int*, int*, double*, unsigned char*);
   unsigned char (*sr_switch_to_mode)(int*, int*, double*, unsigned char*);
   void (*simple_test)(void);
   void (*simple_test_with_params)(int, int, double, unsigned char, unsigned char);
} srAPI;

#ifdef __cplusplus
}
#endif
