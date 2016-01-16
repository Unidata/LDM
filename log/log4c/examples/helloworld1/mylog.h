
#include "log4c.h"

#ifndef LOG_CATEGORY_NAME
#define LOG_CATEGORY_NAME "root"
#endif

#define MYLOGMSG(priority,msg) log_msg(LOG_CATEGORY_NAME,priority,msg)

#ifndef WITHOUT_LOG4C
#define LOG_PRIORITY_ERROR LOG4C_PRIORITY_ERROR
#define LOG_PRIORITY_WARN  LOG4C_PRIORITY_WARN
#define LOG_PRIORITY_NOTICE  LOG4C_PRIORITY_NOTICE
#define LOG_PRIORITY_DEBUG LOG4C_PRIORITY_DEBUG
#define LOG_PRIORITY_TRACE LOG4C_PRIORITY_TRACE
#else
#define LOG_PRIORITY_ERROR 1
#define LOG_PRIORITY_WARN  2
#define LOG_PRIORITY_NOTICE  3
#define LOG_PRIORITY_DEBUG 4
#define LOG_PRIORITY_TRACE 5
#endif

static LOG4C_INLINE int log_init(){
#ifndef WITHOUT_LOG4C
	return(log4c_init());
#else
	return 0;
#endif
}

static LOG4C_INLINE int log_fini(){
#ifndef WITHOUT_LOG4C
	return(log4c_fini());
#else
	return 0;
#endif
}

static LOG4C_INLINE void log_msg(char *catName,int a_priority, char *msg){
#ifndef WITHOUT_LOG4C
	log4c_category_log(log4c_category_get(catName), a_priority, msg);
#else
	printf(msg);
#endif
}

static LOG4C_INLINE int log_setappender(char *catName, char *appName){
#ifndef WITHOUT_LOG4C
	 log4c_category_set_appender(log4c_category_get(catName)
                                  ,log4c_appender_get(appName));
   return(0);
#else
  return(0);
#endif                                  
}

static LOG4C_INLINE void log_log(char *catName,int a_priority,
  const char* a_format,...){
#ifndef WITHOUT_LOG4C
  const log4c_category_t* a_category = log4c_category_get(catName);
if (log4c_category_is_priority_enabled(a_category, a_priority)) {
	va_list va;
	va_start(va, a_format);
	log4c_category_vlog(a_category, a_priority, a_format, va);
	va_end(va);
}
#else
  va_list va;
	va_start(va, a_format);
	vprintf(a_format, va);
	va_end(va);
#endif                                  
}

