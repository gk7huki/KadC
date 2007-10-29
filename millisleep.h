/* Suspend calling thread for millistimeout milliseconds
   It is NOT aborted by signals as nanosleep() is */
void millisleep(unsigned long int millistimeout);
