#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#define LOG_CATEGORY_NAME "log4c.examples.helloworld"
#include "log.h"

int main(int argc, char** argv){
  int rc = 0;
  
  if (log_init()){
    printf("log_init() failed");
    rc = 1;  
  }else{

    MYLOGMSG(LOG4C_PRIORITY_ERROR, "Hello World!");
    
    /* Explicitly call the logging cleanup routine */
    log_fini();
  }
  return rc;
}
